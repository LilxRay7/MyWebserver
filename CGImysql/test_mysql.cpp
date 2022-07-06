#include"sql_connection_pool.h"

int main() {
    connection_pool* coonpool = connection_pool::get_instance();
    coonpool->init("localhost", "root", "root", "yourdb", 3306, 8);
    MYSQL* onesql = nullptr;
    onesql = coonpool->get_connection();
    coonpool->release_connection(onesql);

    return 0;
}