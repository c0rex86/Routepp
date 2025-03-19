#include "../include/config.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <regex>

Config::Config(const std::string& configFile)
    : m_configFile(configFile)
{
}

bool Config::loadConfig()
{
    std::ifstream file(m_configFile);
    if (!file.is_open()) {
        std::cerr << "Ошибка: Не удалось открыть файл конфигурации " << m_configFile << std::endl;
        return false;
    }

    std::string line;
    std::regex routePattern(R"((\S+):(\d+)\s+(\S+):(\d+))");
    
    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '#') {
            continue;
        }

        std::smatch matches;
        if (std::regex_search(line, matches, routePattern)) {
            std::string sourceIP = matches[1];
            int sourcePort = std::stoi(matches[2]);
            std::string destDomain = matches[3];
            int destPort = std::stoi(matches[4]);
            
            m_routes.emplace_back(sourceIP, sourcePort, destDomain, destPort);
            std::cout << "Добавлен маршрут: " << sourceIP << ":" << sourcePort 
                      << " -> " << destDomain << ":" << destPort << std::endl;
        } else {
            std::cerr << "Предупреждение: Некорректная строка конфигурации: " << line << std::endl;
        }
    }

    return !m_routes.empty();
}

const std::vector<Route>& Config::getRoutes() const
{
    return m_routes;
} 