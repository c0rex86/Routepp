#pragma once

#include "config.h"
#include <string>
#include <unordered_map>
#include <vector>
#include <thread>
#include <mutex>
#include <atomic>

class RouteProxy {
public:
    RouteProxy(const Config& config);
    ~RouteProxy();

    bool start();
    
    void stop();

private:
    void handleRoute(const Route& route);
    
    void handleClient(int clientSocket, const Route& route);
    
    void forwardData(int sourceSocket, int destSocket);
    
    std::string resolveDomain(const std::string& domain);
    
    int connectToDestination(const std::string& domain, int port);

    const Config& m_config;
    std::vector<std::thread> m_threads;
    std::vector<int> m_listenSockets;
    std::atomic<bool> m_running;
    std::mutex m_mutex;
};