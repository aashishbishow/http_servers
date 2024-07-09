# Simple HTTP Server in C++ with PHP Integration

This repository contains a simple HTTP server implemented in C++ that serves static files and handles PHP scripts. It supports various HTTP methods and serves content from a specified web root directory.

## Features

- **Static File Serving:** Serve HTML, CSS, JavaScript, images, etc., from a designated directory (`./www` by default).
- **PHP Integration:** Execute PHP scripts via PHP CGI (`/usr/bin/php-cgi` path adjustable).
- **HTTP Methods Supported:** GET, POST, PUT, PATCH, DELETE, HEAD, OPTIONS, COPY, LINK, UNLINK, PURGE, LOCK, UNLOCK, PROPFIND, VIEW.
- **HTTP Response Handling:** Proper status codes and content types for different file types and methods.
- **Multi-threaded Handling:** Handles multiple client connections concurrently using C++ threads.

## Prerequisites

Ensure you have the following installed and set up:

- **C++ Compiler:** Compatible with C++23.
- **PHP CGI:** Adjust the path in `server.cpp` (`PHP_CGI` constant) to match your PHP CGI executable location.

## Usage

1. **Clone the Repository:**
   ```bash
   git clone https://github.com/yourusername/http-server.git
   cd http-server
   ```

2. **Build the Server:**
   ```bash
   g++ -std=c++2b -o server http.cpp -pthread
   ```

3. **Run the Server:**
   ```bash
   ./server
   ```

   By default, the server listens on port `8080`.

4. **Accessing Content:**
   - Place your static files (HTML, CSS, JavaScript, images, etc.) in the `./www` directory.
   - PHP scripts can be placed alongside static files, with the extension `.php`.

5. **Customization:**
   - Adjust `PORT`, `BUFFER_SIZE`, `WEB_ROOT`, and `PHP_CGI` in `http.cpp` as per your requirements.

## Contributing

Feel free to fork this repository, make improvements, and submit pull requests. Contributions are welcome!

## License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.

---