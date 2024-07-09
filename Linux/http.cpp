#include <iostream>
#include <sstream>
#include <fstream>
#include <vector>
#include <thread>
#include <filesystem>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>

#define PORT 8080
#define BUFFER_SIZE 4096
#define WEB_ROOT "./www"
#define PHP_CGI "/usr/bin/php-cgi" // Adjust the path to your PHP CGI executable

namespace fs = std::filesystem;

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
    if (!file.is_open()) {
        return "";
    }
    std::ostringstream contents;
    contents << file.rdbuf();
    file.close();
    return contents.str();
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

void handle_php_request(int client_socket, const std::string& script_path) {
    int child_out_pipe[2];
    pipe(child_out_pipe);

    pid_t pid = fork();
    if (pid == 0) {
        // Child process
        dup2(child_out_pipe[1], STDOUT_FILENO); // Redirect stdout to pipe write end
        close(child_out_pipe[0]); // Close unused pipe read end

        // Execute PHP script
        execl(PHP_CGI, PHP_CGI, "-q", "-f", script_path.c_str(), nullptr);
        exit(EXIT_SUCCESS); // Should not reach here unless execl fails
    } else if (pid > 0) {
        // Parent process
        close(child_out_pipe[1]); // Close unused pipe write end

        // Read from pipe (output from PHP script)
        char buffer[BUFFER_SIZE];
        std::ostringstream response_body;
        ssize_t bytes_read;
        while ((bytes_read = read(child_out_pipe[0], buffer, BUFFER_SIZE)) > 0) {
            response_body.write(buffer, bytes_read);
        }

        // Build HTTP response
        std::string response = build_http_response("200 OK", "text/html", response_body.str());
        send(client_socket, response.c_str(), response.size(), 0);
        close(client_socket);
    } else {
        // Error forking
        std::cerr << "Failed to fork process for PHP execution" << std::endl;
        close(client_socket);
    }
}

void handle_client(int client_socket) {
    char buffer[BUFFER_SIZE];
    int valread = read(client_socket, buffer, BUFFER_SIZE);
    if (valread <= 0) {
        std::cerr << "Failed to read from socket" << std::endl;
        close(client_socket);
        return;
    }

    std::istringstream request(buffer);
    std::string method, path, version;
    request >> method >> path >> version;

    std::cout << "Request: " << method << " " << path << " " << version << std::endl;

    std::string body, response;
    if (method == "GET") {
        if (path == "/") path = "/index.html";
        std::string full_path = WEB_ROOT + path;
        if (fs::exists(full_path) && !fs::is_directory(full_path)) {
            if (full_path.ends_with(".php")) {
                handle_php_request(client_socket, full_path);
                return;
            } else {
                body = get_file_content(full_path);
                response = build_http_response("200 OK", get_content_type(full_path), body);
            }
        } else {
            body = "<html><body><h1>404 Not Found</h1></body></html>";
            response = build_http_response("404 Not Found", "text/html", body);
        }
    } else if (method == "POST" || method == "PUT" || method == "PATCH") {
        // Handle POST/PUT/PATCH requests here
        // Example: Read request body and process accordingly
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
    close(client_socket);
}

int main() {
    int server_fd, new_socket;
    struct sockaddr_in address;
    int opt = 1;
    int addrlen = sizeof(address);

    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        std::cerr << "Socket creation failed" << std::endl;
        return EXIT_FAILURE;
    }

    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt)) != 0) {
        std::cerr << "setsockopt failed" << std::endl;
        close(server_fd);
        return EXIT_FAILURE;
    }

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr*)&address, sizeof(address)) < 0) {
        std::cerr << "Bind failed" << std::endl;
        close(server_fd);
        return EXIT_FAILURE;
    }

    if (listen(server_fd, 10) < 0) {
        std::cerr << "Listen failed" << std::endl;
        close(server_fd);
        return EXIT_FAILURE;
    }

    std::cout << "HTTP server is listening on port " << PORT << std::endl;

    std::vector<std::thread> threads;

    while (true) {
        if ((new_socket = accept(server_fd, (struct sockaddr*)&address, (socklen_t*)&addrlen)) < 0) {
            std::cerr << "Accept failed" << std::endl;
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

    close(server_fd);
    return EXIT_SUCCESS;
}
