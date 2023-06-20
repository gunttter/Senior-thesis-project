#include "main.h"
void post_handler(http_request_t *request, http_response_t *response)
{
    write_response(404, "text/plain", "Hello, world!", response);
}

int main()
{
    // Define the port to listen on
    char *port = "8090";

    // Start the server and listen for incoming requests
    listen_and_serve(port, 20, post_handler);

    return 0;
}