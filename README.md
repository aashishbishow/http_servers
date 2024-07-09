# Simple HTTP Server in C++ with PHP Integration

![GitHub repo size](https://img.shields.io/github/repo-size/aashishbishow/http_servers)
![GitHub license](https://img.shields.io/github/license/aashishbishow/http_servers)

This repository contains implementations of an HTTP server for both Linux (WSL) and Windows environments. The server serves static files and handles PHP scripts, supporting various HTTP methods.

## Table of Contents

- [Features](#features)
- [Prerequisites](#prerequisites)
- [Linux Version (WSL)](#linux-version-wsl)
  - [Compilation](#compilation-linux-wsl)
  - [Usage](#usage-linux-wsl)
- [Windows Version](#windows-version)
  - [Compilation](#compilation-windows)
  - [Usage](#usage-windows)
- [Contributing(#contibuting)]
- [License](#license)

## Features

- **Static File Serving:** Serve HTML, CSS, JavaScript, images, etc., from a designated directory (`./www` by default).
- **PHP Integration:** Execute PHP scripts via PHP CGI (`/usr/bin/php-cgi`{Linux[WSL]} and `:\php\php-cgi.exe`[Windows] path adjustable).
- **HTTP Methods Supported:** GET, POST, PUT, PATCH, DELETE, HEAD, OPTIONS, COPY, LINK, UNLINK, PURGE, LOCK, UNLOCK, PROPFIND, VIEW.
- **HTTP Response Handling:** Proper status codes and content types for different file types and methods.
- **Multi-threaded Handling:** Handles multiple client connections concurrently using C++ threads.

## Prerequisites

- **Linux Version (WSL):**
  - Windows Subsystem for Linux (WSL)
  - C++ compiler compatible with C++23

- **Windows Version:**
  - Visual Studio Code (for Windows version codebase)
  - MinGW (for compilation on Windows)
  - C++ version: C++23, adjust in your code editor configuration


## Linux Version (WSL)

### Compilation

1. **Open a WSL terminal or Linux terminal**
   ```bash 
   # bash for example
   ```

2. **Clone the Repository:**
   ```bash
   git clone https://github.com/aashishbishow/http_servers.git
   ```

3. **Navigate to Linux folder
   ```bash
   cd http_servers/Linux
   ```

4. **Open in a Code editor
   ```bash
   code . 
   # for vs code
   ```

5. **Do the necessary configuration
   - Read [Customization(#customization-linux-wsl)]

6. **Compile the Server:**
   ```bash
   g++ -std=c++2b -o server http.cpp -pthread
   ```

7. **Run the Server:**
   ```bash
   ./server
   ```

### Usage

   By default, the server listens on port `8080`.
   - Type `localhost` in your browser's address bar and hit enter for default `index.html`.
   - Type `localhost:8080/path_to_your_file` in your browser's address bar and hit enter for your specific file.

### Accessing Content:

   - Place your static files (HTML, CSS, JavaScript, images, etc.) in the `./www` directory.
   - PHP scripts can be placed alongside static files with the `.php` extension.

### Customization:

   - Adjust `PORT`, `BUFFER_SIZE`, `WEB_ROOT`, and `PHP_CGI` in `http.cpp` as per your requirements.


## Windows Version

### Compilation

1. **Open a command prompt or terminal(Powershell)**
   ```powershell 
    # for example
   ```

2. **Clone the Repository:**
   ```powershell
   git clone https://github.com/aashishbishow/http_servers.git
   ```

3. **Navigate to Windows folder
   ```powershell
   cd http_servers/Linux
   ```

4. **Open in a Code editor
   ```powershell
   code .
   # for vs code
   ```

5. **Do the necessary configuration
   - Read [Customization(#customization-windows)]

6. **Compile the Server:**
   ```powershell
   g++ -std-c++2b -o server.exe http.cpp -lws2_32
   ```

7. **Run the Server:**
   ```
   ./server
   ```

### Usage

   By default, the server listens on port `8080`.
   - Type `localhost` in your browser's address bar and hit enter to view the default `index.html`.
   - Type `localhost:8080/path_to_your_file` in your browser's address bar and hit enter to access a specific file in `./www`.

#### Accessing Content

   - Place your static files (HTML, CSS, JavaScript, images, etc.) in the `./www` directory.
   - PHP scripts can be placed alongside static files with the `.php` extension.

#### Customization

   - Adjust `PORT`, `BUFFER_SIZE`, `WEB_ROOT`, and `PHP_CGI` in `server.cpp` as per your requirements.


## Contributing

Feel free to fork this repository, make improvements, and submit pull requests. Contributions are welcome!


## License

This project is licensed under the MIT License. See the [LICENSE](LICENSE) file for details.