// SPDX-License-Identifier: BSD-3-Clause

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/sendfile.h>
#include <sys/eventfd.h>
#include <libaio.h>
#include <errno.h>

#include "aws.h"
#include "utils/util.h"
#include "utils/debug.h"
#include "utils/sock_util.h"
#include "utils/w_epoll.h"

/* server socket file descriptor */
static int listenfd;

/* epoll file descriptor */
static int epollfd;

static io_context_t ctx;

// static http_parser request_parser;
// static char request_path[BUFSIZ];	/* storage for request_path */
int connection_send_dynamic(struct connection *conn);

static int aws_on_path_cb(http_parser *p, const char *buf, size_t len)
{
	struct connection *conn = (struct connection *)p->data;

	// printf("aaaaaaaaaaaaaaaaa\n");

	memcpy(conn->request_path, buf, len);
	conn->request_path[len] = '\0';
	conn->have_path = 1;

	return 0;
}

static void connection_prepare_send_reply_header(struct connection *conn)
{
	/* TODO: Prepare the connection buffer to send the reply header. */
	sprintf(conn->send_buffer,
	"HTTP/1.1 200 OK\r\nContent-Length: %d\r\nContent-Type: text/plain\r\n\r\n", (int) conn->file_size);
}

static void connection_prepare_send_404(struct connection *conn)
{
	/* TODO: Prepare the connection buffer to send the 404 header. */
	strcat(conn->send_buffer, "HTTP/1.1 404 Not Found\r\n\r\n");
}

static enum resource_type connection_get_resource_type(struct connection *conn)
{
	/* TODO: Get resource type depending on request path/filename. Filename should
	 * point to the static or dynamic folder.
	 */
	if (strstr(conn->request_path, "static")) {
		strcpy(conn->filename, AWS_ABS_STATIC_FOLDER);
		conn->res_type = RESOURCE_TYPE_STATIC;
		return RESOURCE_TYPE_STATIC;
	}
	if (strstr(conn->request_path, "dynamic")) {
		strcpy(conn->filename, AWS_ABS_DYNAMIC_FOLDER);
		conn->res_type = RESOURCE_TYPE_DYNAMIC;
		return RESOURCE_TYPE_DYNAMIC;
	}
	return RESOURCE_TYPE_NONE;
}


struct connection *connection_create(int sockfd)
{
	/* TODO: Initialize connection structure on given socket. */
	struct connection *connection = malloc(sizeof(*connection));

	connection->sockfd = sockfd;
	memset(connection->recv_buffer, 0, BUFSIZ);
	memset(connection->send_buffer, 0, BUFSIZ);

	return connection;
	return NULL;
}

void connection_start_async_io(struct connection *conn)
{
	/* TODO: Start asynchronous operation (read from file).
	 * Use io_submit(2) & friends for reading data asynchronously.
	 */

	memset(&conn->iocb, 0, sizeof(struct iocb));

	io_prep_pread(&conn->iocb, conn->fd, conn->recv_buffer, conn->recv_len, 0);

	conn->piocb[0] = &conn->iocb;

	if (io_submit(ctx, 1024, conn->piocb) < 0)
		dlog(LOG_ERR, "IO SUBMIT ERROR\n");
}

void connection_remove(struct connection *conn)
{
	/* TODO: Remove connection handler. */
	w_epoll_remove_fd(epollfd, conn->sockfd);
	close(conn->sockfd);
	conn->state = STATE_CONNECTION_CLOSED;
	free(conn);
}

void handle_new_connection(void)
{
	/* TODO: Handle a new connection request on the server socket. */

	static int sockfd;
	socklen_t addrlen = sizeof(struct sockaddr_in);
	struct sockaddr_in addr;
	struct connection *conn;

	/* TODO: Accept new connection. */
	sockfd = accept(listenfd, (SSA *) &addr, &addrlen);

	/* TODO: Set socket to be non-blocking. */
	fcntl(sockfd, F_SETFL, fcntl(sockfd, F_GETFL, 0) | O_NONBLOCK);

	/* TODO: Instantiate new connection handler. */
	conn = connection_create(sockfd);

	/* TODO: Add socket to epoll. */
	w_epoll_add_ptr_in(epollfd, sockfd, conn);

	/* TODO: Initialize HTTP_REQUEST parser. */

	http_parser_init(&conn->request_parser, HTTP_REQUEST);
}

void receive_data(struct connection *conn)
{
	/* TODO: Receive message on socket.
	 * Store message in recv_buffer in struct connection.
	 */

	int bytes_received = 0;

	conn->recv_len = 0;


	while (1) {
		bytes_received = recv(conn->sockfd, conn->recv_buffer + conn->recv_len, BUFSIZ, 0);

		if (bytes_received < 0)
			return;
		else if (bytes_received == 0)
			return;

		conn->recv_len += (size_t) bytes_received;
		conn->recv_buffer[(int) conn->recv_len] = '\0';

		if (strstr(conn->recv_buffer, "\r\n\r\n"))
			break;
	}

	conn->recv_buffer[conn->recv_len] = '\0';
	conn->state = STATE_REQUEST_RECEIVED;
}

int connection_open_file(struct connection *conn)
{
	/* TODO: Open file and update connection fields. */
	int fd = open(conn->request_path, O_WRONLY | O_CREAT | O_TRUNC);

	if (fd < 0) {
		printf("error opening file\n");
		return -1;
	}

	struct stat st;

	if (fstat(fd, &st) < 0) {
		printf("Error at openfile for file stats\n");
		return -1;
	}

	conn->file_size = st.st_size;
	conn->fd = fd;
	conn->file_pos = 0;

	return fd;

	return -1;
}

void connection_complete_async_io(struct connection *conn)
{
	/* TODO: Complete asynchronous operation; operation returns successfully.
	 * Prepare socket for sending.
	 */

	struct io_event events[1];

	int num_events = io_getevents(ctx, 1, 1, events, NULL);

	if (num_events < 0)
		return;


	if (events[0].res < 0)
		return;
}

int parse_header(struct connection *conn)
{
	/* TODO: Parse the HTTP header and extract the file path. */
	/* Use mostly null settings except for on_path callback. */
	http_parser_settings settings_on_path = {
		.on_message_begin = 0,
		.on_header_field = 0,
		.on_header_value = 0,
		.on_path = aws_on_path_cb,
		.on_url = 0,
		.on_fragment = 0,
		.on_query_string = 0,
		.on_body = 0,
		.on_headers_complete = 0,
		.on_message_complete = 0
	};
	conn->request_parser.data = conn;
	http_parser_execute(&conn->request_parser, &settings_on_path, conn->recv_buffer, conn->recv_len);
	return 0;
}

enum connection_state connection_send_static(struct connection *conn)
{
	/* TODO: Send static data using sendfile(2). */
	int file_descriptor = connection_open_file(conn);

	if (file_descriptor < 0)
		return STATE_CONNECTION_CLOSED;


	off_t offset = 0;
	ssize_t remaining = conn->file_size;

	while ((int) remaining > 0) {
		ssize_t sent = sendfile(conn->sockfd, file_descriptor, &offset, remaining);

		if (sent < 0) {
			close(file_descriptor);
			return STATE_CONNECTION_CLOSED;
		}

		remaining = remaining - sent;
	}

	close(file_descriptor);


	return STATE_CONNECTION_CLOSED;

	return STATE_NO_STATE;
}

int connection_send_data(struct connection *conn)
{
	/* May be used as a helper function. */
	/* TODO: Send as much data as possible from the connection send buffer.
	 * Returns the number of bytes sent or -1 if an error occurred
	 */
	if (conn->have_path == 0) {
		connection_prepare_send_404(conn);

		ssize_t sent_header_size = send(conn->sockfd, conn->send_buffer, strlen(conn->send_buffer), 0);

		if (sent_header_size < 0) {
			w_epoll_remove_fd(epollfd, conn->sockfd);
			close(conn->sockfd);
			return -1;
		}
		conn->state = STATE_404_SENT;

		return sent_header_size;
	}

	connection_prepare_send_reply_header(conn);

	ssize_t sent_header_size = send(conn->sockfd, conn->send_buffer, strlen(conn->send_buffer), 0);

	if (sent_header_size < 0)
		return -1;


	if (conn->res_type == RESOURCE_TYPE_STATIC) {
		connection_send_static(conn);
		conn->state = STATE_CONNECTION_CLOSED;
	} else if (conn->res_type == RESOURCE_TYPE_DYNAMIC) {
		connection_send_dynamic(conn);
	}

	return -1;
}


int connection_send_dynamic(struct connection *conn)
{
	/* TODO: Read data asynchronously.
	 * Returns 0 on success and -1 on error.
	 */

	connection_start_async_io(conn);

	struct io_event events[1];
	int num_events = io_getevents(ctx, 1, 1, events, NULL);

	off_t offset = 0;
	ssize_t remaining = events[0].res;

	while (remaining > 0) {
		ssize_t sent = send(conn->sockfd, conn->recv_buffer + offset, remaining, 0);

		if (sent < 0)
			return -1;


		remaining -= sent;
		offset += sent;
	}

	connection_complete_async_io(conn);

	return 0;
}


void handle_input(struct connection *conn)
{
	/* TODO: Handle input information: may be a new message or notification of
	 * completion of an asynchronous I/O operation.
	 */

	switch (conn->state) {
	case STATE_RECEIVING_DATA:
		receive_data(conn);
		parse_header(conn);

		conn->state = STATE_REQUEST_RECEIVED;

		struct epoll_event ev;

		ev.events = EPOLLOUT;
		ev.data.ptr = conn;
		epoll_ctl(epollfd, EPOLL_CTL_MOD, conn->sockfd, &ev);

		enum resource_type resource_type_conn =  connection_get_resource_type(conn);

		conn->res_type = resource_type_conn;

		char *newPath = (char *)malloc(strlen(conn->request_path) + 1);

		newPath[0] = '.';
		if (!(conn->request_path[0] == '.' && conn->request_path[1] != '.'))
			strcpy(newPath + 1, conn->request_path);

		strcpy(conn->request_path, newPath);
		if (access(newPath, F_OK) != -1) {
			conn->have_path = 1;
			free(newPath);
		} else {
			conn->have_path = 0;
		}
		break;
	default:
		printf("shouldn't get here %d\n", conn->state);
	}
}

void handle_output(struct connection *conn)
{
	/* TODO: Handle output information: may be a new valid requests or notification of
	 * completion of an asynchronous I/O operation or invalid requests.
	 */

	switch (conn->state) {
	case STATE_REQUEST_RECEIVED:
		connection_send_data(conn);

	break;
	default:
		ERR("Unexpected state\n");
		exit(1);
	}
}

void handle_client(uint32_t event, struct connection *conn)
{
	/* TODO: Handle new client. There can be input and output connections.
	 * Take care of what happened at the end of a connection.
	 */
	if (event & EPOLLIN) {
		conn->state = STATE_RECEIVING_DATA;
		handle_input(conn);
	}

	if (event & EPOLLOUT) {
		handle_output(conn);
		conn->state = STATE_DATA_SENT;
	}

	if (conn->state == STATE_CONNECTION_CLOSED) {
		w_epoll_remove_fd(epollfd, conn->sockfd);
		close(conn->sockfd);
	}

	if (conn->state == STATE_DATA_SENT) {
		conn->state = STATE_CONNECTION_CLOSED;
		w_epoll_remove_fd(epollfd, conn->sockfd);
		connection_remove(conn);
	}
}

int main(void)
{
	/* TODO: Initialize asynchronous operations. */
	io_setup(1024, &ctx); // prev it was 1024 instead of 1

	/* TODO: Initialize multiplexing. */
	epollfd = w_epoll_create();

	/* TODO: Create server socket. */
	listenfd = tcp_create_listener(AWS_LISTEN_PORT, DEFAULT_LISTEN_BACKLOG);

	/* TODO: Add server socket to epoll object*/
	w_epoll_add_fd_in(epollfd, listenfd);

	/* Uncomment the following line for debugging. */

	/* server main loop */
	while (1) {
		struct epoll_event rev;

		/* TODO: Wait for events. */
		w_epoll_wait_infinite(epollfd, &rev);
		/* TODO: Switch event types; consider
		 *   - new connection requests (on server socket)
		 *   - socket communication (on connection sockets)
		 */
		if (rev.events == EPOLLIN && rev.data.fd == listenfd) {
			handle_new_connection();
		} else if (rev.data.fd != listenfd) {
			struct connection *conn = rev.data.ptr;

			handle_client(rev.events, conn);
		}
	}

	close(listenfd);
	io_destroy(ctx);

	return 0;
}
