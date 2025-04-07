#include <winsock2.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include <dirent.h>
#include <sys/stat.h>

#define PORT 8080
#define BUFFER_SIZE 8192
#define MAX_FILE_SIZE 10485760 // 10MB
#define UPLOAD_DIR "uploads"

// Function to initialize Winsock
void init_winsock() {
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        printf("WSAStartup failed\n");
        exit(1);
    }
}

// Function to send HTTP response to client
void send_response(SOCKET client_sock, const char *header, const char *content_type, const char *body) {
    char response[BUFFER_SIZE];
    sprintf(response, "%s\r\nContent-Type: %s\r\n\r\n%s", header, content_type, body);
    send(client_sock, response, strlen(response), 0);
}

// Function to handle file upload (POST /upload)
void handle_upload(SOCKET client_sock, char *request) {
    // Extract file data from request (simplified)
    char filename[256] = {0};
    char file_data[BUFFER_SIZE];
    int file_size = 0;

    // Find the start of the file data (after headers)
    char *file_start = strstr(request, "\r\n\r\n");
    if (file_start == NULL) {
        send_response(client_sock, "HTTP/1.1 400 Bad Request", "text/plain", "Bad Request: No file data");
        return;
    }

    // Move past the headers
    file_start += 4;

    // Find the Content-Disposition header and extract filename or name
    char *content_disposition = strstr(request, "Content-Disposition:");
    if (content_disposition) {
        // Search for 'filename="' in the Content-Disposition header
        char *filename_start = strstr(content_disposition, "filename=\"");
        if (filename_start) {
            filename_start += 10; // Move past 'filename="'

            // Look for the ending quote for the filename
            char *filename_end = strchr(filename_start, '"');
            if (filename_end) {
                size_t filename_length = filename_end - filename_start;
                if (filename_length < sizeof(filename)) {
                    strncpy(filename, filename_start, filename_length);
                    filename[filename_length] = '\0'; // Null-terminate the filename
                } else {
                    printf("Filename too long, using default.\n");
                    strcpy(filename, "default_upload1");
                }
            } else {
                // If no ending quote, fallback to default
                printf("Filename end not found, using default.\n");
                strcpy(filename, "default_upload2");
            }
        }

        // If 'filename="' is not found, check for 'name='
        if (strlen(filename) == 0) {
            char *name_start = strstr(content_disposition, "name=\"");
            if (name_start) {
                name_start += 6; // Move past 'name="'

                // Look for the ending quote for the name
                char *name_end = strchr(name_start, '"');
                if (name_end) {
                    size_t name_length = name_end - name_start;
                    if (name_length < sizeof(filename)) {
                        strncpy(filename, name_start, name_length);
                        filename[name_length] = '\0'; // Null-terminate the filename
                    } else {
                        printf("Name too long, using default.\n");
                        strcpy(filename, "default_upload3");
                    }
                } else {
                    // If no ending quote, fallback to default
                    printf("Name end not found, using default.\n");
                    strcpy(filename, "default_upload4");
                }
            }
        }
    }

    // If no filename or name was found, use a default name
    if (strlen(filename) == 0) {
        printf("No filename or name found, using default.\n");
        strcpy(filename, "default_upload5");
    }

    // Extract file data (simplified to directly copy from the request body)
    while (*file_start && file_size < MAX_FILE_SIZE) {
        file_data[file_size++] = *file_start++;
    }

    // Ensure file size limit (10MB)
    if (file_size > MAX_FILE_SIZE) {
        send_response(client_sock, "HTTP/1.1 413 Payload Too Large", "text/plain", "File exceeds size limit");
        return;
    }

    // Prepend the uploads directory to the filename
    char full_path[BUFFER_SIZE];
    snprintf(full_path, sizeof(full_path), "%s/%s", UPLOAD_DIR, filename);

    // Write file to disk
    FILE *file = fopen(full_path, "wb");
    if (file == NULL) {
        send_response(client_sock, "HTTP/1.1 500 Internal Server Error", "text/plain", "Failed to write file");
        return;
    }

    fwrite(file_data, 1, file_size, file);
    fclose(file);

    // Send success response
    send_response(client_sock, "HTTP/1.1 200 OK", "text/plain", "File uploaded successfully");
    printf("Request received: %s\n", request);
    printf("Parsed filename: %s\n", filename);
    printf("File saved to: %s\n", full_path);
    printf("File size: %d bytes\n", file_size);
}


// Function to list all files in the "uploads" directory (GET /files)
void handle_list_files(SOCKET client_sock) {
    DIR *dir = opendir(UPLOAD_DIR);
    if (dir == NULL) {
        send_response(client_sock, "HTTP/1.1 500 Internal Server Error", "text/plain", "Unable to read files");
        return;
    }

    char body[BUFFER_SIZE];
    strcpy(body, "[");
    struct dirent *entry;
    int first = 1;
    while ((entry = readdir(dir)) != NULL) {
        struct stat entry_stat;
        char full_path[BUFFER_SIZE];
        snprintf(full_path, sizeof(full_path), "%s/%s", UPLOAD_DIR, entry->d_name);
        if (stat(full_path, &entry_stat) == 0 && S_ISREG(entry_stat.st_mode)) {
            if (!first) {
                strcat(body, ", ");
            }
            strcat(body, "\"");
            strcat(body, entry->d_name);
            strcat(body, "\"");
            first = 0;
        }
    }
    strcat(body, "]");

    closedir(dir);
    send_response(client_sock, "HTTP/1.1 200 OK", "application/json", body);
}

// Function to get file content (GET /file?name=<filename>&limit=<bytes>)
void handle_get_file(SOCKET client_sock, const char *query) {
    char filename[256];
    int limit = 0;

    // Parse the query string for filename and limit
    sscanf(query, "GET /file?name=%255[^&]&limit=%d", filename, &limit);

    // Prepend the uploads directory to the filename
    char full_path[BUFFER_SIZE];
    snprintf(full_path, sizeof(full_path), "%s/%s", UPLOAD_DIR, filename);

    // Open the file
    FILE *file = fopen(full_path, "rb");
    if (file == NULL) {
        send_response(client_sock, "HTTP/1.1 404 Not Found", "text/plain", "File not found");
        return;
    }

    // Read the file and send the first N bytes
    char body[BUFFER_SIZE];
    int bytes_read = fread(body, 1, limit, file);
    body[bytes_read] = '\0'; // Null-terminate the body

    fclose(file);
    send_response(client_sock, "HTTP/1.1 200 OK", "text/plain", body);
}

// Handle incoming client request
void handle_client(SOCKET client_sock) {
    char buffer[BUFFER_SIZE];
    int recv_size = recv(client_sock, buffer, BUFFER_SIZE, 0);

    if (recv_size > 0) {
        buffer[recv_size] = '\0'; // Null-terminate the request

        if (strstr(buffer, "POST /upload") == buffer) {
            handle_upload(client_sock, buffer);
        } else if (strstr(buffer, "GET /files") == buffer) {
            handle_list_files(client_sock);
        } else if (strstr(buffer, "GET /file") == buffer) {
            handle_get_file(client_sock, buffer);
        } else {
            send_response(client_sock, "HTTP/1.1 404 Not Found", "text/plain", "Not Found");
        }
    }

    // Close the client socket
    closesocket(client_sock);
}

// Main function
int main() {
    init_winsock(); // Initialize Winsock

    // Create server socket
    SOCKET server_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (server_sock == INVALID_SOCKET) {
        printf("Socket creation failed\n");
        WSACleanup();
        return 1;
    }

    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    server_addr.sin_addr.s_addr = INADDR_ANY;

    // Bind the server socket to the address and port
    if (bind(server_sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) == SOCKET_ERROR) {
        printf("Bind failed\n");
        closesocket(server_sock);
        WSACleanup();
        return 1;
    }

    // Listen for incoming connections
    if (listen(server_sock, 3) == SOCKET_ERROR) {
        printf("Listen failed\n");
        closesocket(server_sock);
        WSACleanup();
        return 1;
    }

    printf("Server listening on port %d...\n", PORT);

    // Main loop to accept incoming connections
    while (1) {
        SOCKET client_sock = accept(server_sock, NULL, NULL);
        if (client_sock == INVALID_SOCKET) {
            printf("Accept failed\n");
            continue;
        }

        // Handle the client request
        handle_client(client_sock);
    }

    // Close the server socket
    closesocket(server_sock);
    WSACleanup();
    return 0;
}
