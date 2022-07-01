#include "http_conn.h"

// 定义HTTP响应状态
const char* ok_200_title = "OK";
const char* error_400_title = "Bad Request";
const char* error_400_form = "Your request has bad syntax or is inherently impossible to statisfy.\n";
const char* error_403_title = "Forbidden";
const char* error_403_form = "You do not have permission to get file from this server.\n";
const char* error_404_title = "Not Found";
const char* error_404_form = "The requested file was not found on this server.\n";
const char* error_500_title = "Internet Error";
const char* error_500_form = "There was an unusual problem serving the request file.\n";

// 网站根目录
const char* doc_root = "/var/www/html";

// 设置非阻塞I/O
int setnonblocking(int fd) {
    int old_option = fcntl(fd, F_GETFL);
    int new_option = old_option | O_NONBLOCK;
    fcntl(fd, F_SETFL, new_option);
    return old_option;
}

// 注册epoll事件
void addfd(int epollfd, int fd, bool one_shot) {
    epoll_event event;
    event.data.fd = fd;
    event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
    // oneshot代表一个socket连接在任意时刻都只被一个线程处理
    // 注册了EPOLLONESHOT事件的socket一旦被某个线程处理完毕，该线程就应该立即重置这个socket上的EPOLLONESHOT事件
    // 以确保这个socket下一次可读时，其EPOLLIN事件能被触发，进而让其他工作线程有机会继续处理这个socket
    if (one_shot) {
        event.events |= EPOLLONESHOT;
    }
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
    setnonblocking(fd);
}

// 删除epoll事件
void removefd(int epollfd, int fd) {
    epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, 0);
    close(fd);
}

// 修改epoll事件
void modfd(int epollfd, int fd, int ev) {
    epoll_event event;
    event.data.fd = fd;
    event.events = ev | EPOLLET | EPOLLRDHUP | EPOLLONESHOT;
    epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &event);
}

// 类的静态成员在类内声明，类外定义,定义不用加static
// static int m_epollfd;
int http_conn::m_user_count = 0;
int http_conn::m_epollfd = -1;

// 关闭连接
void http_conn::close_conn(bool real_close) {
    if (real_close && (m_sockfd != -1)) {
        removefd(m_epollfd, m_sockfd);
        m_sockfd = -1;
        // 关闭连接，客户数量减一
        m_user_count--;
    }
}

// 初始化新接受的连接
void http_conn::init(int sockfd, const sockaddr_in &addr) {
    m_sockfd = sockfd;
    m_address = addr;
    // 如下两行是为了避免TIME_WAIT状态， 仅用于调试，实际使用时应该去掉
    int reuse = 1;
    setsockopt(m_sockfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    addfd(m_epollfd, sockfd, true);
    m_user_count++;
    init();
}

// 初始化连接
// init() 是 private 函数，被 public 函数 init(int, const sockaddr_in) 调用
void http_conn::init() {
    m_checked_state = CHECK_STATE_REQUESTLINE;
    m_linger = false;
    m_method = GET;
    m_url = 0;
    m_version = 0;
    m_content_length = 0;
    m_host = 0;
    m_start_line = 0;
    m_checked_idx = 0;
    m_read_idx = 0;
    m_write_idx = 0;
    // memset() 常用于内存空间的初始化
    memset(m_read_buf, '\0', READ_BUFFER_SIZE);
    memset(m_write_buf, '\0', WRITE_BUFFER_SIZE);
    memset(m_real_file, '\0', FILENAME_MAX_LEN);
}

// 从状态机
http_conn::LINE_STATUS http_conn::parse_line() {
    char temp;
    // m_checked_idx指向buffer中当前正在分析的字节
    // m_read_idx指向buffer中客户数据的尾部的下一字节
    for (; m_checked_idx < m_read_idx; ++m_checked_idx) {
        // 当前分析的字节
        temp = m_read_buf[m_checked_idx];
        // 判断HTTP请求头部结束的依据是遇到一个空行，仅包含一对回车换行符
        // 如果当前字节是回车符，说明有可能读取到一个完整的行
        if (temp == '\r') {
            // 如果回车符恰巧是buffer中最后一个已被读入的客户数据，说明没有读到一个完整的行
            // 返回LINE_OPEN以表示需要继续读客户数据
            if ((m_checked_idx + 1) == m_read_idx) {
                return LINE_OPEN;
            } else if (m_read_buf[m_checked_idx + 1] == '\n') {
                // 如果下一个字符数换行符，说明成功读取到一个完整的行
                m_read_buf[m_checked_idx++] = '\0';
                m_read_buf[m_checked_idx++] = '\0';
                return LINE_OK;
            }
            // 否则说明客户发送的HTTP请求存在问题
            return LINE_BAD;
        } else if (temp == '\n') {
            // 如果当前字节是换行符，说明也可能读取到一个完整的行
            // 如果上一个字节是回车符，说明成功读取到一个完整的行
            if ((m_checked_idx > 1) && (m_read_buf[m_checked_idx - 1] == '\r')) {
                m_read_buf[m_checked_idx - 1] = '\0';
                m_read_buf[m_checked_idx++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }
    }
    // 需要继续读取数据才能进一步分析
    return LINE_OPEN;
}

// 解析HTTP请求行，获得请求方法、目标URL以及HTTP版本号
http_conn::HTTP_CODE http_conn::parse_request_line(char* text) {
    // char *strpbrk(const char *str1, const char *str2) 检索字符串str1中第一个匹配字符串str2中字符的字符
    // 并返回该字符位置
    m_url = strpbrk(text, " \t");
    // 如果请求行中没有空白字符或者'\t'字符，请求行有问题
    if (!m_url) {
        printf("url error.\n");
        return BAD_REQUEST;
    }
    *m_url++ = '\0';
    char* method = text;
    // strcasecmp忽略大小写比较字符串
    // 仅支持GET方法
    if (strcasecmp(method, "GET") == 0) {
        m_method = GET;
    } else {
        printf("method error.\n");
        return BAD_REQUEST;
    }
    // size_t strspn(const char *str1, const char *str2) 检索字符串str1中第一个不在字符串str2中出现的字符下标
    m_url += strspn(m_url, " \t");
    m_version = strpbrk(m_url, " \t");
    if (!m_version) {
        printf("version error.\n");
        return BAD_REQUEST;
    }
    *m_version++ = '\0';
    m_version += strspn(m_version, " \t");
    // 仅支持HTTP/1.1
    if (strncasecmp(m_version, "HTTP/1.1", 8) != 0) {
        printf("url error: only HTTP/1.1\n");
        return BAD_REQUEST;
    }
    // 检查URL是否合法
    if (strncasecmp(m_url, "http://", 7) == 0) {
        m_url += 7;
        // char *strchr(const char *str, int c) 在参数str所指向的字符串中搜索第一次出现字符c的位置
        m_url = strchr(m_url, '/');
    }
    if (!m_url || m_url[0] != '/') {
        printf("url format error.\n");
        return BAD_REQUEST;
    }
    if (strlen(m_url) == 1) {
        strcat(m_url, "index.html");
    }
    // HTTP请求行处理完毕，状态转移到头部字段的解析
    printf("Parse request line done!\n");
    m_checked_state = CHECK_STATE_HEADER;
    return NO_REQUEST;
}

// 解析HTTP请求的头部信息
http_conn::HTTP_CODE http_conn::parse_headers(char* text) {
    // 空行说明头部字段解析完毕
    if (text[0] == '\0') {
        // 如果HTTP请求有消息体，则还需要读取m_content_length字节的消息体
        // 状态机转移到CHECK_STATE_CONTENT
        if (m_content_length != 0) {
            m_checked_state = CHECK_STATE_CONTENT;
            return NO_REQUEST;
        }
        // 否则说明以及得到了一个完整的HTTP请求
        return GET_REQUEST;
    } else if (strncasecmp(text, "Connection:", 11) == 0) {
        // 处理connection头部字段
        text += 11;
        text += strspn(text, "\t");
        // 是否持续连接
        if (strcasecmp(text, "keep-alive") == 0) {
            m_linger = true;
        }
    } else if (strncasecmp(text, "Content-Length:", 15) == 0) {
        // 处理Content-Length头部字段
        text += 15;
        text += strspn(text, "\t");
        m_content_length = atol(text);
    } else if (strncasecmp(text, "Host:", 5) == 0) {
        text += 5;
        text += strspn(text, "\t");
        m_host = text;
    } else {
        printf("unknow header %s\n", text);
    }
    return NO_REQUEST;
}

// 并没有真正解析HTTP请求的消息体，只是判断它是否被完整的读入了
http_conn::HTTP_CODE http_conn::parse_content(char* text) {
    if (m_read_idx >= (m_content_length + m_checked_idx)) {
        text[m_content_length] = '\0';
        return GET_REQUEST;
    }
    return NO_REQUEST;
}

// 主状态机
http_conn::HTTP_CODE http_conn::process_read() {
    LINE_STATUS line_status = LINE_OK;
    HTTP_CODE ret = NO_REQUEST;
    char* text = 0;

    while (((m_checked_state == CHECK_STATE_CONTENT) && (line_status == LINE_OK))
        || ((line_status = parse_line()) == LINE_OK)) {
            text = get_line();
            m_start_line = m_checked_idx;
            fprintf(stdout, "got 1 http line:\n%s\n", text);

            switch(m_checked_state) {
                case CHECK_STATE_REQUESTLINE: {
                    ret = parse_request_line(text);
                    if (ret == BAD_REQUEST) {
                        return BAD_REQUEST;
                    }
                    break;
                }
                case CHECK_STATE_HEADER: {
                    ret = parse_headers(text);
                    if (ret == BAD_REQUEST) {
                        return BAD_REQUEST;
                    } else if (ret == GET_REQUEST) {
                        return do_request();
                    }
                    break;
                }
                case CHECK_STATE_CONTENT: {
                    ret = parse_content(text);
                    if (ret == GET_REQUEST) {
                        return do_request();
                    }
                    line_status = LINE_OPEN;
                    break;
                }
                default: {
                    return INTERNAL_ERROR;
                }
            }
        }
    return NO_REQUEST;
}

// 当得到一个完整正确的HTTP请求时，我们就分析目标文件的属性，如果目标文件存在
// 并且可读，且不是目录，就用mmap将其映射到内存地址 m_file_address 处，并返回成功获取文件
http_conn::HTTP_CODE http_conn::do_request() {
    // strcpy(m_real_file, doc_root);
    // int len = strlen(doc_root);
    // strncpy(m_real_file + len, m_url, FILENAME_MAX_LEN - len - 1);
    printf("request URL: %s\n", m_url);
    strcpy(m_real_file, m_url + 1);
    if (stat(m_real_file, &m_file_stat) < 0) {
        return NO_RESOURCE;
    }
    // 掩码判断文件其他人是否可读
    if (!(m_file_stat.st_mode & S_IROTH)) {
        return FORBIDDEN_REQUEST;
    }
    // 判断是否是目录
    if (S_ISDIR(m_file_stat.st_mode)) {
        return BAD_REQUEST;
    }
    // 以只读的方式打开
    int fd = open(m_real_file, O_RDONLY);
    // mmap映射到内存中
    m_file_address = (char*)mmap(0, m_file_stat.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    return FILE_REQUEST;
}

// 封装取消映射函数
void http_conn::unmap() {
    if (m_file_address) {
        munmap(m_file_address, m_file_stat.st_size);
        m_file_address = 0;
    }
}

// 循环读取客户数据，直到无数据可读或对方关闭连接
bool http_conn::read() {
    if (m_read_idx >= READ_BUFFER_SIZE) {
        return false;
    }
    // 本轮读到的字节数
    int bytes_read = 0;
    while (true) {
        bytes_read = recv(m_sockfd, m_read_buf + m_read_idx, READ_BUFFER_SIZE - m_read_idx, 0);
        if (bytes_read == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;
            }
            return false;
        } else if (bytes_read == 0) {
            return false;
        }
        m_read_idx += bytes_read;
    }
    return true;
}

// 写HTTP响应
bool http_conn::write() {
    int temp = 0;
    int bytes_have_send = 0;
    int bytes_to_send = m_write_idx;
    if (bytes_to_send == 0) {
        modfd(m_epollfd, m_sockfd, EPOLLIN);
        init();
        return true;
    }

    while(1) {
        // writev() 聚集写，按顺序发送分散内存中的数据
        temp = writev(m_sockfd, m_iv, m_iv_count);
        if (temp <= -1) {
            // 如果TCP写缓冲区没有空间，则等待下一轮EPOLLOUT事件
            // 虽然在此期间服务器无法立即收到同一个客户的下一个请求，但是可以保证连接的完整性
            if (errno == EAGAIN) {
                modfd(m_epollfd, m_sockfd, EPOLLOUT);
                return true;
            }
            unmap();
            return false;
        }
        bytes_to_send -= temp;
        bytes_have_send += temp;
        if (bytes_to_send <= bytes_have_send) {
            // 发送HTTP响应成功，根据HTTP请求中的Connection字段决定是否立即关闭连接
            unmap();
            if (m_linger) {
                init();
                modfd(m_epollfd, m_sockfd, EPOLLIN);
                return true;
            } else {
                modfd(m_epollfd, m_sockfd, EPOLLIN);
                return false;
            }
        }
    }
}

// HTTP响应报文格式
// ＜status-line＞
// ＜headers＞
// ＜blank line＞
// ＜response-body＞

// 往写缓冲区中写入待发送数据
//( , ...) 可变参数
bool http_conn::add_response(const char* format, ...) {
    if (m_write_idx >= WRITE_BUFFER_SIZE) {
        return false;
    }
    // 定义一个 va_list 类型变量
    va_list arg_list;
    va_start(arg_list, format);
    int len = vsnprintf(m_write_buf + m_write_idx, WRITE_BUFFER_SIZE - 1 - m_write_idx, format, arg_list);
    if (len >= (WRITE_BUFFER_SIZE - 1 - m_write_idx)) {
        return false;
    }
    m_write_idx += len;
    // va_start 与 va_end 总是成对出现
    va_end(arg_list);
    return true;
}

bool http_conn::add_status_line(int status, const char* title) {
    return add_response("%s %d %s\r\n", "HTTP/1.1", status, title);
}

bool http_conn::add_headers(int content_len) {
    add_content_length(content_len);
    add_linger();
    add_blank_line();
    return true;
}

bool http_conn::add_content_length(int content_len) {
    return add_response("Content-Length: %d\r\n", content_len);
}

bool http_conn::add_linger() {
    return add_response("Connection %s\r\n", (m_linger == true) ? "keep-alive" : "close");
}

bool http_conn::add_blank_line() {
    return add_response("%s", "\r\n");
}

bool http_conn::add_content(const char* content) {
    return add_response("%s", content);
}

// 根据服务器处理HTTP请求的结果，决定返回给客户端的内容
bool http_conn::process_write(HTTP_CODE ret) {
    switch (ret) {
        case INTERNAL_ERROR: {
            add_status_line(500, error_500_title);
            add_headers(strlen(error_500_form));
            if (!add_content(error_500_form)) {
                return false;
            }
            break;
        }
        case BAD_REQUEST: {
            add_status_line(400, error_400_title);
            add_headers(strlen(error_400_form));
            if (!add_content(error_400_form)) {
                return false;
            }
            break;
        }
        case NO_RESOURCE: {
            add_status_line(404, error_404_title);
            add_headers(strlen(error_404_form));
            if (!add_content(error_404_form)) {
                return false;
            }
            break;
        }
        case FORBIDDEN_REQUEST: {
            add_status_line(403, error_403_title);
            add_headers(strlen(error_403_form));
            if (!add_content(error_403_form)) {
                return false;
            }
            break;
        }
        case FILE_REQUEST: {
            add_status_line(200, ok_200_title);
            if (m_file_stat.st_size != 0) {
                add_headers(m_file_stat.st_size);
                m_iv[0].iov_base = m_write_buf;
                m_iv[0].iov_len = m_write_idx;
                m_iv[1].iov_base = m_file_address;
                m_iv[1].iov_len = m_file_stat.st_size;
                m_iv_count = 2;
                return true;
            } else {
                const char* ok_string = "<html><body>Hello</body></html>";
                add_headers(strlen(ok_string));
                if (!add_content(ok_string)) {
                    return false;
                }
            }
        }
        default: {
            return false;
        }
    }

    m_iv[0].iov_base = m_write_buf;
    m_iv[0].iov_len = m_write_idx;
    m_iv_count = 1;
    return true;
}

// 由线程池的工作线程调用，这是HTTP请求的入口函数
void http_conn::process() {
    HTTP_CODE read_ret = process_read();
    if (read_ret == NO_REQUEST) {
        modfd(m_epollfd, m_sockfd, EPOLLIN);
        return;
    }
    bool write_ret = process_write(read_ret);
    if (!write_ret) {
        close_conn();
    }
    modfd(m_epollfd, m_sockfd, EPOLLOUT);
}