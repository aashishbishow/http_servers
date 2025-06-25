#include <iostream>
#include <fstream>
#include <string>
#include <thread>
#include <mutex>
#include <atomic>
#include <chrono>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <sstream>
#include <algorithm>
#include <filesystem>
#include <memory>
#include <csignal>
#include <cstring>
#include <cstdlib>
#include <cerrno>

// POSIX includes
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <pwd.h>

using namespace std;
namespace fs = std::filesystem;

// Configuration constants
constexpr int SERVER_PORT = 8080;
constexpr const char* WEB_ROOT = "./www";
constexpr const char* SERVER_NAME = "SecureHTTP/1.1";
constexpr size_t MAX_REQUEST_SIZE = 8192;  // 8KB max request
constexpr size_t MAX_BODY_SIZE = 1024 * 1024;  // 1MB max body
constexpr size_t MAX_FILE_SIZE = 10 * 1024 * 1024;  // 10MB max file
constexpr int MAX_CONCURRENT_THREADS = 100;
constexpr int REQUEST_TIMEOUT_SECONDS = 5;
constexpr int MAX_CONNECTIONS_PER_IP = 10;

// Global thread management
atomic<int> active_threads{0};
mutex connections_mutex;
unordered_map<string, int> ip_connections;

// MIME type mappings
const unordered_map<string, string> mime_types = {
    {".html", "text/html"},
    {".htm", "text/html"},
    {".css", "text/css"},
    {".js", "application/javascript"},
    {".json", "application/json"},
    {".png", "image/png"},
    {".jpg", "image/jpeg"},
    {".jpeg", "image/jpeg"},
    {".gif", "image/gif"},
    {".svg", "image/svg+xml"},
    {".ico", "image/x-icon"},
    {".txt", "text/plain"},
    {".pdf", "application/pdf"},
    {".xml", "application/xml"}
};

// HTTP status codes
const unordered_map<int, string> status_messages = {
    {200, "OK"},
    {400, "Bad Request"},
    {403, "Forbidden"},
    {404, "Not Found"},
    {405, "Method Not Allowed"},
    {413, "Payload Too Large"},
    {414, "URI Too Long"},
    {500, "Internal Server Error"},
    {503, "Service Unavailable"}
};

struct HttpRequest {
    string method;
    string path;
    string version;
    unordered_map<string, string> headers;
    string body;
    bool valid = false;
};

struct HttpResponse {
    int status_code = 200;
    unordered_map<string, string> headers;
    string body;
    vector<uint8_t> binary_data;
    bool is_binary = false;
};

// Utility functions
void log_error(const string& message) {
    cerr << "[ERROR] " << chrono::system_clock::now().time_since_epoch().count() 
         << ": " << message << endl;
}

void log_info(const string& message) {
    cout << "[INFO] " << chrono::system_clock::now().time_since_epoch().count() 
         << ": " << message << endl;
}

// URL decode function
string url_decode(const string& encoded) {
    string decoded;
    decoded.reserve(encoded.length());
    
    for (size_t i = 0; i < encoded.length(); ++i) {
        if (encoded[i] == '%' && i + 2 < encoded.length()) {
            try {
                int value = stoi(encoded.substr(i + 1, 2), nullptr, 16);
                if (value >= 0 && value <= 255) {
                    decoded += static_cast<char>(value);
                    i += 2;
                } else {
                    decoded += encoded[i];
                }
            } catch (...) {
                decoded += encoded[i];
            }
        } else if (encoded[i] == '+') {
            decoded += ' ';
        } else {
            decoded += encoded[i];
        }
    }
    return decoded;
}

// Sanitize path to prevent directory traversal
string sanitize_path(const string& path) {
    if (path.empty() || path[0] != '/') {
        return "";
    }
    
    // URL decode first
    string decoded = url_decode(path);
    
    // Reject paths with null bytes or other dangerous characters
    if (decoded.find('\0') != string::npos || 
        decoded.find("..") != string::npos ||
        decoded.find('\\') != string::npos) {
        return "";
    }
    
    // Convert to filesystem path
    fs::path requested_path = fs::path(WEB_ROOT) / decoded.substr(1);
    
    try {
        // Get canonical path to resolve any remaining .. or symlinks
        fs::path canonical = fs::canonical(fs::path(WEB_ROOT));
        fs::path target;
        
        if (fs::exists(requested_path)) {
            target = fs::canonical(requested_path);
        } else {
            target = fs::weakly_canonical(requested_path);
        }
        
        // Ensure the resolved path is within web root
        string canonical_str = canonical.string();
        string target_str = target.string();
        
        if (target_str.substr(0, canonical_str.length()) != canonical_str) {
            return "";  // Path traversal attempt
        }
        
        return target.string();
    } catch (const fs::filesystem_error&) {
        return "";
    }
}

// Check if file is forbidden (hidden/system files)
bool is_forbidden_file(const string& path) {
    fs::path p(path);
    string filename = p.filename().string();
    
    // Reject hidden files and dangerous extensions
    if (filename.empty() || filename[0] == '.' ||
        filename == "Thumbs.db" || filename == "desktop.ini" ||
        path.find("/.") != string::npos) {
        return true;
    }
    
    // Check for system/config file patterns
    static const unordered_set<string> forbidden_patterns = {
        ".htaccess", ".htpasswd", ".git", ".svn", ".env", 
        "web.config", ".DS_Store", "__pycache__"
    };
    
    for (const auto& pattern : forbidden_patterns) {
        if (filename.find(pattern) != string::npos) {
            return true;
        }
    }
    
    return false;
}

// Get MIME type from file extension
string get_mime_type(const string& path) {
    fs::path p(path);
    string ext = p.extension().string();
    transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    
    auto it = mime_types.find(ext);
    return (it != mime_types.end()) ? it->second : "application/octet-stream";
}

// Read file with size limit
bool read_file_safe(const string& filepath, vector<uint8_t>& content) {
    try {
        ifstream file(filepath, ios::binary);
        if (!file.is_open()) {
            return false;
        }
        
        // Get file size
        file.seekg(0, ios::end);
        size_t file_size = file.tellg();
        file.seekg(0, ios::beg);
        
        if (file_size > MAX_FILE_SIZE) {
            log_error("File too large: " + filepath);
            return false;
        }
        
        content.resize(file_size);
        file.read(reinterpret_cast<char*>(content.data()), file_size);
        
        return file.good() || file.eof();
    } catch (...) {
        return false;
    }
}

// Parse HTTP request with proper validation
HttpRequest parse_request(const string& raw_request) {
    HttpRequest request;
    istringstream stream(raw_request);
    string line;
    
    // Parse request line
    if (!getline(stream, line) || line.empty()) {
        return request;
    }
    
    // Remove carriage return if present
    if (!line.empty() && line.back() == '\r') {
        line.pop_back();
    }
    
    istringstream request_line(line);
    if (!(request_line >> request.method >> request.path >> request.version)) {
        return request;
    }
    
    // Validate HTTP method
    static const unordered_set<string> allowed_methods = {"GET", "POST", "HEAD"};
    if (allowed_methods.find(request.method) == allowed_methods.end()) {
        return request;
    }
    
    // Validate HTTP version
    if (request.version != "HTTP/1.0" && request.version != "HTTP/1.1") {
        return request;
    }
    
    // Parse headers
    size_t content_length = 0;
    while (getline(stream, line) && !line.empty() && line != "\r") {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        
        size_t colon_pos = line.find(':');
        if (colon_pos != string::npos) {
            string header_name = line.substr(0, colon_pos);
            string header_value = line.substr(colon_pos + 1);
            
            // Trim whitespace
            header_name.erase(0, header_name.find_first_not_of(" \t"));
            header_name.erase(header_name.find_last_not_of(" \t") + 1);
            header_value.erase(0, header_value.find_first_not_of(" \t"));
            header_value.erase(header_value.find_last_not_of(" \t") + 1);
            
            // Convert header name to lowercase for case-insensitive comparison
            transform(header_name.begin(), header_name.end(), header_name.begin(), ::tolower);
            
            request.headers[header_name] = header_value;
            
            if (header_name == "content-length") {
                try {
                    content_length = stoull(header_value);
                    if (content_length > MAX_BODY_SIZE) {
                        return request;  // Body too large
                    }
                } catch (...) {
                    return request;  // Invalid content-length
                }
            }
        }
    }
    
    // Read body if present
    if (content_length > 0) {
        request.body.resize(content_length);
        stream.read(&request.body[0], content_length);
        if (stream.gcount() != static_cast<streamsize>(content_length)) {
            return request;  // Incomplete body
        }
    }
    
    request.valid = true;
    return request;
}

// Execute PHP script safely
bool execute_php(const string& script_path, const HttpRequest& request, string& output) {
    // Validate PHP file extension and location
    if (!script_path.ends_with(".php")) {
        return false;
    }
    
    // Ensure script is within web root
    try {
        fs::path canonical_root = fs::canonical(WEB_ROOT);
        fs::path canonical_script = fs::canonical(script_path);
        
        if (canonical_script.string().substr(0, canonical_root.string().length()) != canonical_root.string()) {
            return false;
        }
    } catch (...) {
        return false;
    }
    
    // Create pipes for communication
    int pipe_fd[2];
    if (pipe(pipe_fd) == -1) {
        log_error("Failed to create pipe: " + string(strerror(errno)));
        return false;
    }
    
    pid_t pid = fork();
    if (pid == -1) {
        log_error("Failed to fork: " + string(strerror(errno)));
        close(pipe_fd[0]);
        close(pipe_fd[1]);
        return false;
    }
    
    if (pid == 0) {
        // Child process
        close(pipe_fd[0]);  // Close read end
        
        // Redirect stdout to pipe
        if (dup2(pipe_fd[1], STDOUT_FILENO) == -1) {
            exit(1);
        }
        close(pipe_fd[1]);
        
        // Set environment variables
        setenv("REQUEST_METHOD", request.method.c_str(), 1);
        setenv("SCRIPT_FILENAME", script_path.c_str(), 1);
        setenv("REQUEST_URI", request.path.c_str(), 1);
        
        if (!request.body.empty()) {
            setenv("CONTENT_LENGTH", to_string(request.body.length()).c_str(), 1);
        }
        
        // Execute PHP
        execl("/usr/bin/php", "php", "-f", script_path.c_str(), nullptr);
        exit(1);  // execl failed
    } else {
        // Parent process
        close(pipe_fd[1]);  // Close write end
        
        // Read output with timeout
        fd_set read_fds;
        struct timeval timeout;
        timeout.tv_sec = 5;  // 5 second timeout
        timeout.tv_usec = 0;
        
        FD_ZERO(&read_fds);
        FD_SET(pipe_fd[0], &read_fds);
        
        int select_result = select(pipe_fd[0] + 1, &read_fds, nullptr, nullptr, &timeout);
        
        if (select_result > 0 && FD_ISSET(pipe_fd[0], &read_fds)) {
            char buffer[4096];
            ssize_t bytes_read;
            
            while ((bytes_read = read(pipe_fd[0], buffer, sizeof(buffer) - 1)) > 0) {
                buffer[bytes_read] = '\0';
                output += buffer;
                
                // Limit output size
                if (output.length() > MAX_FILE_SIZE) {
                    output.resize(MAX_FILE_SIZE);
                    break;
                }
            }
        }
        
        close(pipe_fd[0]);
        
        // Wait for child process
        int status;
        waitpid(pid, &status, 0);
        
        return WIFEXITED(status) && WEXITSTATUS(status) == 0;
    }
}

// Handle directory requests
HttpResponse handle_directory(const string& dir_path, const string& url_path) {
    HttpResponse response;
    
    // Check for index files
    vector<string> index_files = {"index.html", "index.htm", "index.php"};
    
    for (const string& index_file : index_files) {
        string index_path = dir_path + "/" + index_file;
        if (fs::exists(index_path) && fs::is_regular_file(index_path)) {
            if (index_file == "index.php") {
                HttpRequest dummy_request;
                dummy_request.method = "GET";
                dummy_request.path = url_path + index_file;
                
                string php_output;
                if (execute_php(index_path, dummy_request, php_output)) {
                    response.body = php_output;
                    response.headers["Content-Type"] = "text/html";
                    return response;
                }
            } else {
                vector<uint8_t> content;
                if (read_file_safe(index_path, content)) {
                    response.binary_data = move(content);
                    response.is_binary = true;
                    response.headers["Content-Type"] = get_mime_type(index_path);
                    return response;
                }
            }
        }
    }
    
    // No index file found - return 403 Forbidden
    response.status_code = 403;
    response.body = "<html><body><h1>403 Forbidden</h1><p>Directory listing is not allowed.</p></body></html>";
    response.headers["Content-Type"] = "text/html";
    return response;
}

// Process HTTP request
HttpResponse process_request(const HttpRequest& request, const string& client_ip) {
    HttpResponse response;
    
    if (!request.valid) {
        response.status_code = 400;
        response.body = "<html><body><h1>400 Bad Request</h1></body></html>";
        response.headers["Content-Type"] = "text/html";
        return response;
    }
    
    // Sanitize path
    string safe_path = sanitize_path(request.path);
    if (safe_path.empty()) {
        response.status_code = 403;
        response.body = "<html><body><h1>403 Forbidden</h1><p>Invalid path.</p></body></html>";
        response.headers["Content-Type"] = "text/html";
        return response;
    }
    
    // Check for forbidden files
    if (is_forbidden_file(safe_path)) {
        response.status_code = 403;
        response.body = "<html><body><h1>403 Forbidden</h1><p>Access denied.</p></body></html>";
        response.headers["Content-Type"] = "text/html";
        return response;
    }
    
    // Check if file/directory exists
    if (!fs::exists(safe_path)) {
        response.status_code = 404;
        response.body = "<html><body><h1>404 Not Found</h1><p>The requested resource was not found.</p></body></html>";
        response.headers["Content-Type"] = "text/html";
        return response;
    }
    
    // Handle directory
    if (fs::is_directory(safe_path)) {
        return handle_directory(safe_path, request.path);
    }
    
    // Handle PHP files
    if (safe_path.ends_with(".php")) {
        string php_output;
        if (execute_php(safe_path, request, php_output)) {
            response.body = php_output;
            response.headers["Content-Type"] = "text/html";
        } else {
            response.status_code = 500;
            response.body = "<html><body><h1>500 Internal Server Error</h1><p>PHP execution failed.</p></body></html>";
            response.headers["Content-Type"] = "text/html";
        }
        return response;
    }
    
    // Handle static files
    vector<uint8_t> content;
    if (read_file_safe(safe_path, content)) {
        response.binary_data = move(content);
        response.is_binary = true;
        response.headers["Content-Type"] = get_mime_type(safe_path);
    } else {
        response.status_code = 500;
        response.body = "<html><body><h1>500 Internal Server Error</h1><p>Failed to read file.</p></body></html>";
        response.headers["Content-Type"] = "text/html";
    }
    
    return response;
}

// Send HTTP response
bool send_response(int client_socket, const HttpResponse& response) {
    string status_message = "Unknown";
    auto it = status_messages.find(response.status_code);
    if (it != status_messages.end()) {
        status_message = it->second;
    }
    
    // Build response headers
    stringstream response_stream;
    response_stream << "HTTP/1.1 " << response.status_code << " " << status_message << "\r\n";
    response_stream << "Server: " << SERVER_NAME << "\r\n";
    response_stream << "Connection: close\r\n";
    
    // Add custom headers
    for (const auto& header : response.headers) {
        response_stream << header.first << ": " << header.second << "\r\n";
    }
    
    // Content length
    size_t content_length = response.is_binary ? response.binary_data.size() : response.body.length();
    response_stream << "Content-Length: " << content_length << "\r\n";
    
    response_stream << "\r\n";
    
    // Send headers
    string headers = response_stream.str();
    if (send(client_socket, headers.c_str(), headers.length(), MSG_NOSIGNAL) == -1) {
        return false;
    }
    
    // Send body
    if (content_length > 0) {
        if (response.is_binary) {
            if (send(client_socket, response.binary_data.data(), response.binary_data.size(), MSG_NOSIGNAL) == -1) {
                return false;
            }
        } else {
            if (send(client_socket, response.body.c_str(), response.body.length(), MSG_NOSIGNAL) == -1) {
                return false;
            }
        }
    }
    
    return true;
}

// Read request with timeout
string read_request_with_timeout(int client_socket) {
    string request;
    char buffer[1024];
    
    fd_set read_fds;
    struct timeval timeout;
    
    while (request.length() < MAX_REQUEST_SIZE) {
        FD_ZERO(&read_fds);
        FD_SET(client_socket, &read_fds);
        timeout.tv_sec = REQUEST_TIMEOUT_SECONDS;
        timeout.tv_usec = 0;
        
        int select_result = select(client_socket + 1, &read_fds, nullptr, nullptr, &timeout);
        
        if (select_result <= 0) {
            break;  // Timeout or error
        }
        
        ssize_t bytes_received = recv(client_socket, buffer, sizeof(buffer) - 1, 0);
        if (bytes_received <= 0) {
            break;
        }
        
        buffer[bytes_received] = '\0';
        request += buffer;
        
        // Check if we have complete headers
        if (request.find("\r\n\r\n") != string::npos) {
            break;
        }
    }
    
    return request;
}

// Handle client connection
void handle_client(int client_socket, const string& client_ip) {
    // Increment active thread count
    active_threads++;
    
    // Track connection per IP
    {
        lock_guard<mutex> lock(connections_mutex);
        ip_connections[client_ip]++;
    }
    
    try {
        // Read request with timeout
        string raw_request = read_request_with_timeout(client_socket);
        
        if (raw_request.empty()) {
            log_error("Empty or timeout request from " + client_ip);
        } else {
            // Parse and process request
            HttpRequest request = parse_request(raw_request);
            HttpResponse response = process_request(request, client_ip);
            
            // Send response
            if (!send_response(client_socket, response)) {
                log_error("Failed to send response to " + client_ip);
            }
            
            log_info("Served " + request.method + " " + request.path + " to " + client_ip + 
                    " (Status: " + to_string(response.status_code) + ")");
        }
    } catch (const exception& e) {
        log_error("Exception handling client " + client_ip + ": " + e.what());
    }
    
    // Cleanup
    close(client_socket);
    
    {
        lock_guard<mutex> lock(connections_mutex);
        ip_connections[client_ip]--;
        if (ip_connections[client_ip] <= 0) {
            ip_connections.erase(client_ip);
        }
    }
    
    active_threads--;
}

// Signal handler for SIGCHLD to prevent zombie processes
void sigchld_handler(int sig) {
    (void)sig;  // Suppress unused parameter warning
    while (waitpid(-1, nullptr, WNOHANG) > 0) {
        // Reap zombie processes
    }
}

// Main server function
int main() {
    // Install SIGCHLD handler
    struct sigaction sa;
    sa.sa_handler = sigchld_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART | SA_NOCLDSTOP;
    if (sigaction(SIGCHLD, &sa, nullptr) == -1) {
        log_error("Failed to install SIGCHLD handler");
        return 1;
    }
    
    // Create socket
    int server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket == -1) {
        log_error("Failed to create socket: " + string(strerror(errno)));
        return 1;
    }
    
    // Set socket options
    int opt = 1;
    if (setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == -1) {
        log_error("Failed to set socket options: " + string(strerror(errno)));
        close(server_socket);
        return 1;
    }
    
    // Bind socket
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(SERVER_PORT);
    
    if (bind(server_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) == -1) {
        log_error("Failed to bind socket: " + string(strerror(errno)));
        close(server_socket);
        return 1;
    }
    
    // Listen for connections
    if (listen(server_socket, 128) == -1) {
        log_error("Failed to listen: " + string(strerror(errno)));
        close(server_socket);
        return 1;
    }
    
    log_info("Secure HTTP Server started on port " + to_string(SERVER_PORT));
    log_info("Web root: " + string(WEB_ROOT));
    log_info("Max threads: " + to_string(MAX_CONCURRENT_THREADS));
    
    // Main server loop
    while (true) {
        struct sockaddr_in client_addr;
        socklen_t client_addr_len = sizeof(client_addr);
        
        int client_socket = accept(server_socket, (struct sockaddr*)&client_addr, &client_addr_len);
        if (client_socket == -1) {
            if (errno == EINTR) {
                continue;  // Interrupted by signal, continue
            }
            log_error("Failed to accept connection: " + string(strerror(errno)));
            continue;
        }
        
        // Get client IP
        string client_ip = inet_ntoa(client_addr.sin_addr);
        
        // Check thread limit
        if (active_threads >= MAX_CONCURRENT_THREADS) {
            log_error("Thread limit reached, rejecting connection from " + client_ip);
            
            string error_response = "HTTP/1.1 503 Service Unavailable\r\n"
                                  "Content-Type: text/html\r\n"
                                  "Content-Length: 82\r\n"
                                  "Connection: close\r\n\r\n"
                                  "<html><body><h1>503 Service Unavailable</h1><p>Server busy.</p></body></html>";
            send(client_socket, error_response.c_str(), error_response.length(), MSG_NOSIGNAL);
            close(client_socket);
            continue;
        }
        
        // Check connections per IP
        {
            lock_guard<mutex> lock(connections_mutex);
            if (ip_connections[client_ip] >= MAX_CONNECTIONS_PER_IP) {
                log_error("Too many connections from " + client_ip);
                
                string error_response = "HTTP/1.1 429 Too Many Requests\r\n"
                                      "Content-Type: text/html\r\n"
                                      "Content-Length: 86\r\n"
                                      "Connection: close\r\n\r\n"
                                      "<html><body><h1>429 Too Many Requests</h1><p>Rate limited.</p></body></html>";
                send(client_socket, error_response.c_str(), error_response.length(), MSG_NOSIGNAL);
                close(client_socket);
                continue;
            }
        }
        
        // Handle client in new thread
        thread client_thread(handle_client, client_socket, client_ip);
        client_thread.detach();
    }
    
    close(server_socket);
    return 0;
}