#include "../include/config.h"
#include "../include/proxy.h"
#include <iostream>
#include <csignal>
#include <cstring>
#include <string>

RouteProxy* g_proxy = nullptr;

void signalHandler(int signal)
{
    std::cout << "Получен сигнал " << signal << ", завершение работы..." << std::endl;
    if (g_proxy) {
        g_proxy->stop();
    }
}

void setupSignalHandlers()
{
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = signalHandler;
    sigaction(SIGINT, &sa, nullptr);
    sigaction(SIGTERM, &sa, nullptr);
}

void printUsage(const char* programName)
{
    std::cout << "Использование: " << programName << " <файл_конфигурации>" << std::endl;
    std::cout << "Формат конфигурации:" << std::endl;
    std::cout << "  source_ip:source_port destination_domain:destination_port" << std::endl;
    std::cout << "Пример:" << std::endl;
    std::cout << "  10.0.0.5:100 nana.zaza.com:100" << std::endl;
    std::cout << "  10.0.0.6:100 dada.zov:100" << std::endl;
}

int main(int argc, char* argv[])
{
    if (argc != 2) {
        printUsage(argv[0]);
        return 1;
    }

    std::string configFile = argv[1];
    
    Config config(configFile);
    if (!config.loadConfig()) {
        std::cerr << "Ошибка: Не удалось загрузить конфигурацию из " << configFile << std::endl;
        return 1;
    }
    
    setupSignalHandlers();
    
    RouteProxy proxy(config);
    g_proxy = &proxy;
    
    if (!proxy.start()) {
        std::cerr << "Ошибка: Не удалось запустить сервер" << std::endl;
        return 1;
    }
    
    std::cout << "Сервер запущен. Нажмите Ctrl+C для завершения." << std::endl;
    
    while (true) {
        sleep(1);
    }
    
    return 0;
} 