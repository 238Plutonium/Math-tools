/*
 * mycord client - Part 2 (AI-improved version)
 * Improved code quality, error handling, and bug fixes
 */

#include <stdbool.h>
#include <stdio.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <pthread.h>
#include <signal.h>
#include <ctype.h>
#include <stdint.h>

// Message types from protocol specification
enum MessageType {
    MSG_LOGIN        = 0,    // outbound: client login
    MSG_LOGOUT       = 1,    // outbound: client logout
    MSG_MESSAGE_SEND = 2,    // outbound: send message
    MSG_MESSAGE_RECV = 10,   // inbound: receive message
    MSG_DISCONNECT   = 12,   // inbound: server disconnect
    MSG_SYSTEM       = 13    // inbound: system message
};
typedef enum MessageType message_type_t;

// 1064-byte message structure (packed for network transmission)
struct __attribute__((packed)) Message {
    uint32_t type, timestamp;
    char username[32], message[1024];
};
typedef struct Message message_t;

// Global client settings
typedef struct Settings {
    struct sockaddr_in server;
    bool quiet;
    int socket_fd;
    bool running;
    char username[32];
} settings_t;

// ANSI color codes for terminal output
static const char* COLOR_RED = "\033[31m";
static const char* COLOR_GRAY = "\033[90m";
static const char* COLOR_RESET = "\033[0m";

static settings_t settings = {0};

// Forward declarations
static int resolve_address(const char *host, const char *ip, bool is_domain);
static bool validate_string(bool is_message, const char *str);
static void display_message(const char *username, const char *message, uint32_t timestamp);
static int send_message(int type, const char *username, const char *message);
static ssize_t perform_full_write(const void *buf, size_t n);

// Parse command line arguments and setup server address
int process_args(int argc, char *argv[]) {
    const char *ip = "127.0.0.1";
    const char *domain = NULL;
    int port = 8080;
    bool has_ip = false, has_domain = false;//innitialize default values
    
    for (int i = 1; i < argc; i++) {
        const char *arg = argv[i];
        
        if (!strcmp(arg, "-h") || !strcmp(arg, "--help")) {
            printf("mycord client\n\n");
            printf("options:\n");
            printf("  --help                show this help message and exit\n");
            printf("  --port PORT           port to connect to (default: 8080)\n");
            printf("  --ip IP               IP to connect to (default: \"127.0.0.1\")\n");
            printf("  --domain DOMAIN       Domain name to connect to (if domain is\n");
            printf("                        specified, IP must not be)\n");
            printf("  --quiet               disable mention highlighting\n");
            exit(0);//print help message
        }
        else if (!strcmp(arg, "--port")) {
            if (++i >= argc) {
                fprintf(stderr, "Error: --port value missing\n");
                return -1;
            }//parse port
            port = atoi(argv[i]);
            if (port <= 0 || port > 65535) {
                fprintf(stderr, "Error: invalid port\n");
                return -1;
            }//validate port
        }
        else if (!strcmp(arg, "--ip")) {
            if (++i >= argc) {
                fprintf(stderr, "Error: missing ip\n");
                return -1;//parse ip
            }
            has_ip = true;
            ip = argv[i];
        }
        else if (!strcmp(arg, "--domain")) {
            if (++i >= argc) {
                fprintf(stderr, "Error: missing domain\n");
                return -1;//parse domain
            }
            has_domain = true;
            domain = argv[i];
        }
        else if (!strcmp(arg, "--quiet")) {
            settings.quiet = true;//parse quiet
        }
        else {
            fprintf(stderr, "Error: unknown arg \"%s\"\n", arg);
            return -1;//parse unknown arg
        }
    }
    
    if (has_domain && has_ip) {
        fprintf(stderr, "Error: ip and domain both present\n");
        return -1;
    }
    
    // Setup sockaddr structure
    settings.server.sin_family = AF_INET;
    settings.server.sin_port = htons(port);
    
    if (resolve_address(domain, ip, has_domain) < 0) {
        return -1;
    }
    
    return 0;
}

// Resolve hostname or IP address into server address structure
static int resolve_address(const char *host, const char *ip, bool is_domain) {
    if (is_domain) {
        struct hostent *hostent = gethostbyname(host);
        if (hostent && hostent->h_addr_list[0]) {
            memcpy(&settings.server.sin_addr, hostent->h_addr_list[0], hostent->h_length);
            return 0;
        } else {//resolve host
            fprintf(stderr, "Error: cannot resolve host\n");
            return -1;
        }
    }
    
    // Parse IPv4 address
    if (inet_pton(AF_INET, ip, &settings.server.sin_addr) != 1) {
        fprintf(stderr, "Error: invalid ipv4 addr\n");
        return -1;
    }
    
    return 0;
}

// Validate string: mode=true for messages, mode=false for usernames
static bool validate_string(bool is_message, const char *str) {
    for (const unsigned char *p = (const unsigned char *)str; *p; p++) {
        if (is_message ? !isprint(*p) : !isalnum(*p)) {
            fprintf(stderr, "Error: invalid character\n");
            return false;
        }
    }
    return true;
}

// Get username from whoami command
int get_username() {
    char buffer[64] = {0};
    FILE *fp = popen("whoami", "r");
    //popem to see of "whoami" is successful
    if (!fp) {
        fprintf(stderr, "Error: can't retrieve username\n");
        return -1;
    }
    //attempt tp get username
    if (fgets(buffer, sizeof(buffer), fp) == NULL) {
        pclose(fp);
        fprintf(stderr, "Error: username not found\n");
        return -1;
    }
    pclose(fp);
    
    // Strip newline if present
    size_t len = strlen(buffer);
    if (len > 0 && buffer[len - 1] == '\n') {
        buffer[len - 1] = '\0';
        len--;
    }
    
    if (len == 0) {
        fprintf(stderr, "Error: username not found\n");
        return -1;
    }//check if username is found
    
    if (!validate_string(false, buffer)) {
        fprintf(stderr, "Error: invalid username\n");
        return -1;
    }//validate username
    
    strcpy(settings.username, buffer);
    return 0;
}

// Signal handler for graceful shutdown
void handle_signal(int signal) {
    (void)signal;  // Suppress unused parameter warning
    settings.running = false;
    close(STDIN_FILENO);  // Unblock getline()
}

// Read exactly n bytes from socket (handles partial reads)
ssize_t perform_full_read(void *buf, size_t n) {
    char *data = (char *)buf;
    size_t received = 0;
    
    while (received < n) {
        ssize_t bytes = read(settings.socket_fd, data + received, n - received);
        //read bytes from socket
        if (bytes > 0) {
            received += bytes;
            continue;
        }
        
        if (!settings.running) return -1;  // Shutdown requested
        if (bytes == 0) return -1;         // EOF/connection closed
        
        // Handle EINTR (signal interrupt)
        if (errno == EINTR) continue;
        
        // Real error
        return -1;
    }
    
    return received;
}

// Write exactly n bytes to socket (handles partial writes)
static ssize_t perform_full_write(const void *buf, size_t n) {
    const char *data = (const char *)buf;
    size_t written = 0;
    
    while (written < n) {
        ssize_t bytes = write(settings.socket_fd, data + written, n - written);
        
        if (bytes > 0) {
            written += bytes;
            continue;
        }
        
        if (!settings.running) return -1;  // Shutdown requested
        
        // Handle EINTR (signal interrupt)
        if (errno == EINTR) continue;
        
        // Real error
        return -1;
    }
    
    return written;
}

// Send message to server
static int send_message(int type, const char *username, const char *message) {
    message_t msg;
    memset(&msg, 0, sizeof(msg));
    
    msg.type = htonl(type);
    msg.timestamp = htonl(time(NULL));
    //if useranme and message exist
    if (username) {
        strncpy(msg.username, username, sizeof(msg.username) - 1);
    }
    if (message) {
        strncpy(msg.message, message, sizeof(msg.message) - 1);
    }
    //full write
    return (perform_full_write(&msg, sizeof(msg)) == sizeof(msg)) ? 0 : -1;
}

// Display chat message with timestamp and mention highlighting
static void display_message(const char *username, const char *message, uint32_t timestamp) {
    // Format timestamp
    time_t time_val = timestamp;
    char time_str[32];
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", localtime(&time_val));
    
    printf("[%s] %s: ", time_str, username);
    
    // Quiet mode: just print message
    if (settings.quiet) {
        puts(message);
        fflush(stdout);
        return;
    }
    
    // Look for @username mentions and highlight them
    char mention[33];
    snprintf(mention, sizeof(mention), "@%s", settings.username);
    size_t mention_len = strlen(mention);
    
    const char *current = message;
    const char *found;
    
    while ((found = strstr(current, mention)) != NULL) {
        // Print text before mention
        fwrite(current, 1, found - current, stdout);
        
        // Print mention in red with bell alert
        printf("\a%s%s%s", COLOR_RED, mention, COLOR_RESET);
        
        current = found + mention_len;
    }
    
    // Print remaining text
    puts(current);
    fflush(stdout);
}

// Thread function: receive and process messages from server
void* receive_messages_thread(void* arg) {
    (void)arg;  // Suppress unused parameter warning
    
    message_t msg;
    uint32_t type, timestamp;
    
    while (settings.running && perform_full_read(&msg, sizeof(msg)) >= 0) {
        type = ntohl(msg.type);
        timestamp = ntohl(msg.timestamp);
        
        if (type == MSG_MESSAGE_RECV) {//receicing system message
            display_message(msg.username, msg.message, timestamp);
        }
        else if (type == MSG_SYSTEM) {//receicing system message
            printf("%s[SYSTEM] %s%s\n", COLOR_GRAY, msg.message, COLOR_RESET);
            fflush(stdout);
        }
        else if (type == MSG_DISCONNECT) {//receiving disconnect
            printf("%s[DISCONNECT] %s%s\n", COLOR_RED, msg.message, COLOR_RESET);
            fflush(stdout);
            settings.running = false;
	    kill(getpid(),SIGINT);
           
            // Cleanup before exit
	}
        else {
            fprintf(stderr, "Error: type \"%u\" is unknown\n", type);
        }
        
        if (!settings.running) break;
    }
    
    settings.running = false;
    return NULL;
}

// Validate message before sending
bool txvalid(const char *msg) {
    if (!msg) return false;
    
    int len = strlen(msg);
    if (len <= 0) {
        fprintf(stderr, "Error: msg too short\n");
        return false;
    }//msg too short?
    if (len > 1023) {
        fprintf(stderr, "Error: msg too long\n");
        return false;//msg too long?
    }
    
    return validate_string(true, msg);
}

// Wrapper for send_message (kept for compatibility)
int tx(int type, const char *username, const char *message) {
    return send_message(type, username, message);
}

int main(int argc, char *argv[]) {
    // Setup signal handlers
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handle_signal;
    sigemptyset(&sa.sa_mask);
    
    if (sigaction(SIGINT, &sa, NULL) != 0 || sigaction(SIGTERM, &sa, NULL) != 0) {
        fprintf(stderr, "Error: signal setup\n");
        exit(1);
    }
    
    // Parse arguments and get username
    if (process_args(argc, argv) != 0 || get_username() != 0) {
        exit(1);
    }
    
    // Create socket
    settings.socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (settings.socket_fd < 0) {
        fprintf(stderr, "Error: socket\n");
        exit(1);
    }
    
    // Connect to server
    if (connect(settings.socket_fd, (struct sockaddr*)&settings.server, sizeof(settings.server)) < 0) {
        fprintf(stderr, "Error: connect\n");
        close(settings.socket_fd);
        exit(1);
    }
    
    settings.running = true;
    
    // Send login message
    if (send_message(MSG_LOGIN, settings.username, NULL) < 0) {
        fprintf(stderr, "Error: login\n");
        close(settings.socket_fd);
        exit(1);
    }
    
    // Start receiver thread
    pthread_t thread;
    if (pthread_create(&thread, NULL, receive_messages_thread, NULL) != 0) {
        fprintf(stderr, "Error: thread\n");
        close(settings.socket_fd);
        exit(1);
    }
    
    // Main loop: read user input and send messages
    char *line = NULL;
    size_t line_size = 0;
    
    while (settings.running) {
        ssize_t read_bytes = getline(&line, &line_size, stdin);
        
        if (read_bytes < 0) {
            if (feof(stdin)) {
                break;  // EOF (Ctrl+D)
            }
            if (errno == EINTR && settings.running) {//continue if interrupted but still running
                clearerr(stdin);
                continue;
            }
            if (errno != EINTR || !settings.running) {//break if not running or the read error isn't due to interrupt
                break;
            }
            continue;
        }
        
        // Strip newline characters
        while (read_bytes > 0 && (line[read_bytes - 1] == '\n' || line[read_bytes - 1] == '\r')) {
            line[--read_bytes] = '\0';
        }
        
        // Validate and send message
        if (txvalid(line)) {
            if (send_message(MSG_MESSAGE_SEND, NULL, line) < 0 && settings.running) {
                fprintf(stderr, "Error: send\n");
                settings.running = false;
            }
        }
    }
    
    // Cleanup: send logout and wait for thread
    send_message(MSG_LOGOUT, NULL, NULL);
    settings.running = false;
    
    free(line);
    shutdown(settings.socket_fd, SHUT_RDWR);  // Unblock receiver thread
    close(settings.socket_fd);
    pthread_join(thread, NULL);
    
    return 0;
}
