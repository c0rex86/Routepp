#include "../include/proxy.h"
#include <iostream>
#include <cstring>
#include <cerrno>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <fcntl.h>
#include <poll.h>

#define BUFFER_SIZE 8192

RouteProxy::RouteProxy(const Config& config)
    : m_config(config), m_running(false)
{
}

RouteProxy::~RouteProxy()
{
    stop();
}

bool RouteProxy::start()
{
    if (m_running) {
        std::cout << "Сервер уже запущен" << std::endl;
        return true;
    }

    const auto& routes = m_config.getRoutes();
    if (routes.empty()) {
        std::cerr << "Ошибка: Нет настроенных маршрутов" << std::endl;
        return false;
    }

    m_running = true;

    for (const auto& route : routes) {
        m_threads.emplace_back(&RouteProxy::handleRoute, this, route);
    }

    std::cout << "Сервер успешно запущен" << std::endl;
    return true;
}

void RouteProxy::stop()
{
    if (!m_running) {
        return;
    }

    m_running = false;

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (int socket : m_listenSockets) {
            close(socket);
        }
        m_listenSockets.clear();
    }

    for (auto& thread : m_threads) {
        if (thread.joinable()) {
            thread.join();
        }
    }
    m_threads.clear();

    std::cout << "Сервер остановлен" << std::endl;
}

void RouteProxy::handleRoute(const Route& route)
{
    int serverSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (serverSocket < 0) {
        std::cerr << "Ошибка при создании сокета: " << strerror(errno) << std::endl;
        return;
    }

    int opt = 1;
    if (setsockopt(serverSocket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        std::cerr << "Ошибка при установке опций сокета: " << strerror(errno) << std::endl;
        close(serverSocket);
        return;
    }
    
    if (setsockopt(serverSocket, IPPROTO_IP, IP_TRANSPARENT, &opt, sizeof(opt)) < 0) {
        std::cerr << "Предупреждение: Не удалось установить IP_TRANSPARENT: " << strerror(errno) << std::endl;
    }

    struct sockaddr_in serverAddr;
    memset(&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(route.sourcePort);
    
    if (route.sourceIP == "0.0.0.0" || route.sourceIP == "*") {
        serverAddr.sin_addr.s_addr = INADDR_ANY;
        std::cout << "Прослушивание на всех интерфейсах (0.0.0.0) на порту " << route.sourcePort << std::endl;
    } else {
        if (inet_pton(AF_INET, route.sourceIP.c_str(), &serverAddr.sin_addr) <= 0) {
            std::cerr << "Неверный IP-адрес: " << route.sourceIP << std::endl;
            close(serverSocket);
            return;
        }
    }

    if (bind(serverSocket, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) < 0) {
        std::cerr << "Ошибка при привязке сокета к " << route.sourceIP << ":" 
                  << route.sourcePort << ": " << strerror(errno) << std::endl;
        std::cerr << "Попытка прослушивания на 0.0.0.0 (все интерфейсы) вместо " << route.sourceIP << std::endl;
        
        memset(&serverAddr, 0, sizeof(serverAddr));
        serverAddr.sin_family = AF_INET;
        serverAddr.sin_port = htons(route.sourcePort);
        serverAddr.sin_addr.s_addr = INADDR_ANY;
        
        if (bind(serverSocket, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) < 0) {
            std::cerr << "Ошибка при привязке сокета к 0.0.0.0:" 
                      << route.sourcePort << ": " << strerror(errno) << std::endl;
            close(serverSocket);
            return;
        } else {
            std::cout << "Успешная привязка к 0.0.0.0:" << route.sourcePort << " вместо " 
                      << route.sourceIP << ":" << route.sourcePort << std::endl;
        }
    }

    if (listen(serverSocket, 10) < 0) {
        std::cerr << "Ошибка при прослушивании сокета: " << strerror(errno) << std::endl;
        close(serverSocket);
        return;
    }

    std::cout << "Прослушивание " << route.sourceIP << ":" << route.sourcePort 
              << " -> " << route.destDomain << ":" << route.destPort << std::endl;

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_listenSockets.push_back(serverSocket);
    }

    while (m_running) {
        struct sockaddr_in clientAddr;
        socklen_t clientAddrSize = sizeof(clientAddr);
        
        int clientSocket = accept(serverSocket, (struct sockaddr*)&clientAddr, &clientAddrSize);
        if (clientSocket < 0) {
            if (errno == EINTR || errno == ECONNABORTED) {
                continue;
            }
            std::cerr << "Ошибка при принятии соединения: " << strerror(errno) << std::endl;
            break;
        }

        char clientIP[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &clientAddr.sin_addr, clientIP, INET_ADDRSTRLEN);
        std::cout << "Принято соединение от " << clientIP << ":" << ntohs(clientAddr.sin_port) << std::endl;

        std::thread clientThread(&RouteProxy::handleClient, this, clientSocket, route);
        clientThread.detach();
    }

    close(serverSocket);
}

void RouteProxy::handleClient(int clientSocket, const Route& route)
{
    int destSocket = connectToDestination(route.destDomain, route.destPort);
    if (destSocket < 0) {
        close(clientSocket);
        return;
    }

    fcntl(clientSocket, F_SETFL, O_NONBLOCK);
    fcntl(destSocket, F_SETFL, O_NONBLOCK);
    
    struct pollfd fds[2];
    fds[0].fd = clientSocket;
    fds[0].events = POLLIN;
    fds[1].fd = destSocket;
    fds[1].events = POLLIN;
    
    char buffer[BUFFER_SIZE];
    bool clientClosed = false;
    bool destClosed = false;
    
    while (!clientClosed && !destClosed && m_running) {
        int ret = poll(fds, 2, 1000);
        if (ret < 0) {
            std::cerr << "Ошибка при опросе сокетов: " << strerror(errno) << std::endl;
            break;
        }
        
        if (ret == 0) {
            continue;
        }
        
        if (fds[0].revents & POLLIN) {
            ssize_t bytesRead = recv(clientSocket, buffer, BUFFER_SIZE, 0);
            if (bytesRead <= 0) {
                clientClosed = true;
            } else {
                ssize_t bytesSent = 0;
                while (bytesSent < bytesRead) {
                    ssize_t sent = send(destSocket, buffer + bytesSent, bytesRead - bytesSent, 0);
                    if (sent <= 0) {
                        destClosed = true;
                        break;
                    }
                    bytesSent += sent;
                }
            }
        }
        
        if (fds[1].revents & POLLIN) {
            ssize_t bytesRead = recv(destSocket, buffer, BUFFER_SIZE, 0);
            if (bytesRead <= 0) {
                destClosed = true;
            } else {
                ssize_t bytesSent = 0;
                while (bytesSent < bytesRead) {
                    ssize_t sent = send(clientSocket, buffer + bytesSent, bytesRead - bytesSent, 0);
                    if (sent <= 0) {
                        clientClosed = true;
                        break;
                    }
                    bytesSent += sent;
                }
            }
        }
        
        if ((fds[0].revents & (POLLERR | POLLHUP | POLLNVAL)) ||
            (fds[1].revents & (POLLERR | POLLHUP | POLLNVAL))) {
            break;
        }
    }
    
    close(clientSocket);
    close(destSocket);
}

std::string RouteProxy::resolveDomain(const std::string& domain)
{
    struct addrinfo hints, *result;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    
    int status = getaddrinfo(domain.c_str(), nullptr, &hints, &result);
    if (status != 0) {
        std::cerr << "Ошибка при разрешении домена " << domain << ": " 
                  << gai_strerror(status) << std::endl;
        return "";
    }
    
    char ipStr[INET_ADDRSTRLEN];
    for (struct addrinfo* p = result; p != nullptr; p = p->ai_next) {
        struct sockaddr_in* ipv4 = (struct sockaddr_in*)p->ai_addr;
        inet_ntop(AF_INET, &(ipv4->sin_addr), ipStr, INET_ADDRSTRLEN);
        freeaddrinfo(result);
        return std::string(ipStr);
    }
    
    freeaddrinfo(result);
    return "";
}

int RouteProxy::connectToDestination(const std::string& domain, int port)
{
    std::string destIP = resolveDomain(domain);
    if (destIP.empty()) {
        std::cerr << "Не удалось разрешить домен: " << domain << std::endl;
        return -1;
    }
    
    int destSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (destSocket < 0) {
        std::cerr << "Ошибка при создании сокета: " << strerror(errno) << std::endl;
        return -1;
    }
    
    struct sockaddr_in destAddr;
    memset(&destAddr, 0, sizeof(destAddr));
    destAddr.sin_family = AF_INET;
    destAddr.sin_port = htons(port);
    
    if (inet_pton(AF_INET, destIP.c_str(), &destAddr.sin_addr) <= 0) {
        std::cerr << "Неверный IP-адрес: " << destIP << std::endl;
        close(destSocket);
        return -1;
    }
    
    if (connect(destSocket, (struct sockaddr*)&destAddr, sizeof(destAddr)) < 0) {
        std::cerr << "Ошибка при подключении к " << domain << "(" << destIP << "):" 
                  << port << ": " << strerror(errno) << std::endl;
        close(destSocket);
        return -1;
    }
    
    std::cout << "Подключен к " << domain << "(" << destIP << "):" << port << std::endl;
    return destSocket;
}

void RouteProxy::forwardData(int sourceSocket, int destSocket)
{
    char buffer[BUFFER_SIZE];
    ssize_t bytesRead;
    
    bytesRead = recv(sourceSocket, buffer, BUFFER_SIZE, 0);
    if (bytesRead <= 0) {
        return;
    }
    
    send(destSocket, buffer, bytesRead, 0);
} 