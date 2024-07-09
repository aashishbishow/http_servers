#include <iostream>
#include <cstring>
#include <thread>
#include <vector>
#include <sstream>
#include <fstream>
#include <unordered_map>
#include <filesystem>
#include <winsock2.h>
#include <ws2tcpip.h>
#define PORT 8080
#define BUFFER_SIZE 4096
#define WEB_ROOT "./www"

namespace fs = std::filesystem;

#pragma comment(lib, "ws2_32.lib")

std::string build_http_response(const std::string& status, const std::string& content_type, const std::string& body) {
    std::ostringstream response;
    response << "HTTP/1.1 " << status << "\r\n";
    response << "Content-Length: " << body.size() << "\r\n";
    response << "Content-Type: " << content_type << "\r\n";
    response << "Connection: close\r\n";
    response << "\r\n";
    response << body;
    return response.str();
}

std::string get_file_content(const std::string& path) {
    std::ifstream file(path, std::ios::in | std::ios::binary);
    if (file) {
        std::ostringstream contents;
        contents << file.rdbuf();
        file.close();
        return contents.str();
    }
    return "";
}

std::string get_content_type(const std::string& path) {
    if (path.ends_with(".html")) return "text/html";
    if (path.ends_with(".css")) return "text/css";
    if (path.ends_with(".js")) return "application/javascript";
    if (path.ends_with(".png")) return "image/png";
    if (path.ends_with(".jpg") || path.ends_with(".jpeg")) return "image/jpeg";
    if (path.ends_with(".gif")) return "image/gif";
    return "text/plain";
}

void handle_client(SOCKET client_socket) {
    char buffer[BUFFER_SIZE];
    int valread;

    // Read data from the client
    valread = recv(client_socket, buffer, BUFFER_SIZE, 0);
    if (valread <= 0) {
        std::cerr << "Failed to read from socket" << std::endl;
        closesocket(client_socket);
        return;
    }

    // Simple request parsing
    std::istringstream request(buffer);
    std::string method, path, version;
    request >> method >> path >> version;

    std::cout << "Request: " << method << " " << path << " " << version << std::endl;

    std::string body, response;
    if (method == "GET") {
        if (path == "/") path = "/index.html";
        std::string full_path = WEB_ROOT + path;
        if (fs::exists(full_path) && !fs::is_directory(full_path)) {
            body = get_file_content(full_path);
            response = build_http_response("200 OK", get_content_type(full_path), body);
        } else {
            body = "<html><body><h1>404 Not Found</h1></body></html>";
            response = build_http_response("404 Not Found", "text/html", body);
        }
    } else if (method == "POST" || method == "PUT" || method == "PATCH") {
        body = buffer + request.tellg();
        response = build_http_response("200 OK", "text/plain", "Received " + method + " data:\n" + body);
    } else if (method == "DELETE") {
        response = build_http_response("200 OK", "text/plain", "DELETE method received");
    } else if (method == "HEAD") {
        response = build_http_response("200 OK", "text/plain", "");
    } else if (method == "OPTIONS") {
        response = build_http_response("200 OK", "text/plain", "OPTIONS method received");
    } else if (method == "COPY") {
        response = build_http_response("200 OK", "text/plain", "COPY method received");
    } else if (method == "LINK") {
        response = build_http_response("200 OK", "text/plain", "LINK method received");
    } else if (method == "UNLINK") {
        response = build_http_response("200 OK", "text/plain", "UNLINK method received");
    } else if (method == "PURGE") {
        response = build_http_response("200 OK", "text/plain", "PURGE method received");
    } else if (method == "LOCK") {
        response = build_http_response("200 OK", "text/plain", "LOCK method received");
    } else if (method == "UNLOCK") {
        response = build_http_response("200 OK", "text/plain", "UNLOCK method received");
    } else if (method == "PROPFIND") {
        response = build_http_response("200 OK", "text/plain", "PROPFIND method received");
    } else if (method == "VIEW") {
        response = build_http_response("200 OK", "text/plain", "VIEW method received");
    } else {
        body = "<html><body><h1>405 Method Not Allowed</h1></body></html>";
        response = build_http_response("405 Method Not Allowed", "text/html", body);
    }

    send(client_socket, response.c_str(), response.size(), 0);
    memset(buffer, 0, BUFFER_SIZE);
    closesocket(client_socket);
}

int main() {
    WSADATA wsaData;
    int iResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (iResult != 0) {
        std::cerr << "WSAStartup failed: " << iResult << std::endl;
        return 1;
    }

    SOCKET server_fd, new_socket;
    struct sockaddr_in address;
    int opt = 1;
    int addrlen = sizeof(address);

    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == INVALID_SOCKET) {
        std::cerr << "Socket creation failed: " << WSAGetLastError() << std::endl;
        WSACleanup();
        return 1;
    }

    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, (char*)&opt, sizeof(opt)) == SOCKET_ERROR) {
        std::cerr << "setsockopt failed: " << WSAGetLastError() << std::endl;
        closesocket(server_fd);
        WSACleanup();
        return 1;
    }

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr*)&address, sizeof(address)) == SOCKET_ERROR) {
        std::cerr << "Bind failed: " << WSAGetLastError() << std::endl;
        closesocket(server_fd);
        WSACleanup();
        return 1;
    }

    if (listen(server_fd, 10) == SOCKET_ERROR) {
        std::cerr << "Listen failed: " << WSAGetLastError() << std::endl;
        closesocket(server_fd);
        WSACleanup();
        return 1;
    }

    std::cout << "HTTP server is listening on port " << PORT << std::endl;

    std::vector<std::thread> threads;

    while (true) {
        if ((new_socket = accept(server_fd, (struct sockaddr*)&address, (int*)&addrlen)) == INVALID_SOCKET) {
            std::cerr << "Accept failed: " << WSAGetLastError() << std::endl;
            continue;
        }

        std::cout << "Connection accepted" << std::endl;
        threads.emplace_back(handle_client, new_socket);
    }

    for (auto& th : threads) {
        if (th.joinable()) {
            th.join();
        }
    }

    closesocket(server_fd);
    WSACleanup();
    return 0;
}
