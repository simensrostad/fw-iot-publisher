#include <coap_backend.h>
#include <net/socket.h>
#include <net/cloud.h>
#include <net/cloud_backend.h>
#include <net/coap.h>
#include <stdio.h>
#include <net/tls_credentials.h>

#include <logging/log.h>

LOG_MODULE_REGISTER(coap_backend, CONFIG_COAP_BACKEND_LOG_LEVEL);

BUILD_ASSERT_MSG(sizeof(CONFIG_COAP_BACKEND_SERVER_HOST_NAME) > 1,
		 "CoAP server hostname not set");

static struct sockaddr_storage host_addr;

static int client_fd;
static u16_t next_token;

#define APP_COAP_VERSION 1

static u8_t coap_buf[CONFIG_COAP_BACKEND_RX_TX_BUFFER_LEN];

#if !defined(CONFIG_CLOUD_API)
static coap_backend_evt_handler_t module_evt_handler;
#endif

#if defined(CONFIG_CLOUD_API)
static struct cloud_backend *coap_backend;
#endif

#if !defined(CONFIG_CLOUD_API)
static void coap_backend_notify_event(const struct coap_backend_evt *evt)
{
	if ((module_evt_handler != NULL) && (evt != NULL)) {
		module_evt_handler(evt);
	}
}
#endif

static int server_resolve(void)
{
	int err;
	struct addrinfo *result;
	struct addrinfo hints = {
		.ai_family = AF_INET,
		.ai_socktype = SOCK_DGRAM
	};
	char ipv4_addr[NET_IPV4_ADDR_LEN];

	err = getaddrinfo(CONFIG_COAP_BACKEND_SERVER_HOST_NAME, NULL, &hints,
			  &result);
	if (err != 0) {
		LOG_ERR("ERROR: getaddrinfo failed %d", err);
		return -EIO;
	}

	if (result == NULL) {
		LOG_ERR("ERROR: Address not found");
		return -ENOENT;
	}

	/* IPv4 Address. */
	struct sockaddr_in *server4 = ((struct sockaddr_in *)&host_addr);

	server4->sin_addr.s_addr =
		((struct sockaddr_in *)result->ai_addr)->sin_addr.s_addr;
	server4->sin_family = AF_INET;
	server4->sin_port = htons(CONFIG_COAP_BACKEND_SERVER_PORT);

	inet_ntop(AF_INET, &server4->sin_addr.s_addr, ipv4_addr,
		  sizeof(ipv4_addr));
	LOG_DBG("IPv4 Address found %s", log_strdup(ipv4_addr));

	/* Free the address. */
	freeaddrinfo(result);

	return 0;
}

int coap_backend_ping(void)
{
	int err;
	struct coap_packet ping;

	next_token = 0;

	err = coap_packet_init(&ping, coap_buf, sizeof(coap_buf),
			       APP_COAP_VERSION, COAP_TYPE_CON,
			       0, NULL, 0, coap_next_id());
	if (err < 0) {
		LOG_ERR("Failed to create CoAP request, %d", err);
		return err;
	}

	err = send(client_fd, ping.data, ping.offset, 0);
	if (err < 0) {
		LOG_ERR("Failed to send CoAP PING, %d", errno);
		return -errno;
	}

	LOG_DBG("CoAP PING sent: token 0x%04x", next_token);

	return 0;
}

int coap_backend_input(void)
{
	int err, received;
	struct coap_packet reply;
	const u8_t *payload;
	u16_t payload_len;
	u8_t token[8];
	u16_t token_len;
	u8_t temp_buf[16];

	received = recv(client_fd, coap_buf, sizeof(coap_buf), MSG_DONTWAIT);
	if (received == EAGAIN || received == EWOULDBLOCK) {
		LOG_DBG("socket EAGAIN");
		goto exit;
	} else if (received < 0) {
		LOG_DBG("Socket error, exit...");
#if defined(CONFIG_CLOUD_API)
		struct cloud_event error_event = {
			.type = CLOUD_EVT_ERROR,
		};
		cloud_notify_event(coap_backend, &error_event, NULL);
#else
		struct coap_backend_event error_evt = {
			.type = COAP_BACKEND_EVT_ERROR,
		};
		coap_backend_notify_event(&error_evt);
		return -ESOCKTNOSUPPORT;
#endif
	}

	if (received == 0) {
		LOG_ERR("Empty datagram");
		goto exit;
	}

	err = coap_packet_parse(&reply, coap_buf, received, NULL, 0);
	if (err < 0) {
		LOG_ERR("Malformed response received: %d", err);
#if defined(CONFIG_CLOUD_API)
		struct cloud_event error_event = {
			.type = CLOUD_EVT_ERROR,
		};
		cloud_notify_event(coap_backend, &error_event, NULL);
#else
		struct coap_backend_event error_evt = {
			.type = COAP_BACKEND_EVT_ERROR,
		};
		coap_backend_notify_event(&error_evt);
		return -ESOCKTNOSUPPORT;
#endif
	}

	payload = coap_packet_get_payload(&reply, &payload_len);
	token_len = coap_header_get_token(&reply, token);

	if ((token_len != sizeof(next_token)) &&
	    (memcmp(&next_token, token, sizeof(next_token)) != 0)) {
		LOG_DBG("Invalid token received: 0x%02x%02x",
		       token[1], token[0]);
		goto exit;
	}

	// snprintf(temp_buf, MAX(payload_len, sizeof(temp_buf)), "%s", payload);

	LOG_DBG("CoAP response: code: 0x%x, token 0x%02x%02x, payload: %s\n",
	       coap_header_get_code(&reply), token[1], token[0], temp_buf);

exit:
	return 0;
}

int coap_backend_send(const struct coap_backend_tx_data *const tx_data)
{
	int err;
	struct coap_packet request;

	struct coap_backend_tx_data tx_data_send = {
		.str = tx_data->str,
		.len = tx_data->len
	};

	next_token++;

	err = coap_packet_init(&request, coap_buf, sizeof(coap_buf),
			       APP_COAP_VERSION, COAP_TYPE_NON_CON,
			       sizeof(next_token), (u8_t *)&next_token,
			       COAP_METHOD_PUT, coap_next_id());
	if (err < 0) {
		LOG_ERR("Failed to create CoAP request, %d", err);
		return err;
	}

	err = coap_packet_append_option(&request, COAP_OPTION_URI_PATH,
					(u8_t *)CONFIG_COAP_BACKEND_RESOURCE,
					strlen(CONFIG_COAP_BACKEND_RESOURCE));
	if (err < 0) {
		LOG_ERR("Failed to encode CoAP option, %d", err);
		return err;
	}

	err = coap_packet_append_payload_marker(&request);
	if (err < 0) {
		LOG_ERR("Failed to encode CoAP payload marker, %d", err);
		return err;
	}

	err = coap_packet_append_payload(&request, tx_data_send.str,
					 tx_data_send.len);
	if (err < 0) {
		LOG_ERR("Failed to encode CoAP payload, %d", err);
		return err;
	}

	err = send(client_fd, request.data, request.offset, 0);
	if (err < 0) {
		LOG_ERR("Failed to send CoAP request, %d", errno);
		return -errno;
	}

	LOG_DBG("CoAP request sent: token 0x%04x", next_token);

	return 0;
}

int coap_backend_disconnect(void)
{
	return close(client_fd);
}


int coap_backend_connect(struct coap_backend_config *const config)
{
	int ret = 0;

#if defined(CONFIG_COAP_BACKEND_DTLS_ENABLE)

	sec_tag_t tls_tag_list[] = {
		CONFIG_COAP_BACKEND_SEC_TAG,
	};

	client_fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_DTLS_1_2);
	if (client_fd < 0) {
		LOG_ERR("Failed to create CoAP socket: %d.", errno);
		return -errno;
	}

	ret = setsockopt(client_fd, SOL_TLS, TLS_SEC_TAG_LIST,
			 tls_tag_list, sizeof(tls_tag_list));
	if (ret < 0) {
		LOG_ERR("Failed to set TLS_SEC_TAG_LIST option: %d", errno);
		goto error;
	}

#else
	client_fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (client_fd < 0) {
		LOG_ERR("Failed to create CoAP socket: %d.", errno);
		goto error;
	}
#endif

	ret = connect(client_fd, (struct sockaddr *)&host_addr,
		      sizeof(struct sockaddr_in));
	if (ret < 0) {
		LOG_ERR("Connect failed : %d", errno);
		goto error;
	}

#if !defined(CONFIG_CLOUD_API)
	config->socket = client_fd;
#endif

	next_token = sys_rand32_get();

	return 0;

error:
	(void)close(client_fd);
	return -errno;
}

int coap_backend_init(const struct coap_backend_config *const config,
		      coap_backend_evt_handler_t event_handler)
{
	return server_resolve();
}

#if defined(CONFIG_CLOUD_API)
static int c_init(const struct cloud_backend *const backend,
		  cloud_evt_handler_t handler)
{
	backend->config->handler = handler;
	coap_backend = (struct cloud_backend *)backend;

	return 0;
}

static int c_connect(const struct cloud_backend *const backend)
{
	int err;

	err = coap_backend_init(NULL, NULL);
	if (err) {
		LOG_ERR("coap_backend_init, error: err");
		return err;
	}

	err = coap_backend_connect(NULL);
	if (err) {
		return err;
	}

	backend->config->socket = client_fd;

	struct cloud_event connected_event = {
		.type = CLOUD_EVT_CONNECTED,
	};
	backend->config->handler(backend, &connected_event, NULL);

	struct cloud_event ready_event = {
		.type = CLOUD_EVT_READY,
	};
	backend->config->handler(backend, &ready_event, NULL);

	return err;
}

static int c_disconnect(const struct cloud_backend *const backend)
{
	return coap_backend_disconnect();
}

static int c_send(const struct cloud_backend *const backend,
		  const struct cloud_msg *const msg)
{
	struct coap_backend_tx_data tx_data = {
		.str = msg->buf,
		.len = msg->len,
	};

	return coap_backend_send(&tx_data);
}

static int c_input(const struct cloud_backend *const backend)
{
	return coap_backend_input();
}

static int c_ping(const struct cloud_backend *const backend)
{
	return coap_backend_ping();
}

static int c_keepalive_time_left(const struct cloud_backend *const backend)
{
	return K_SECONDS(CONFIG_COAP_BACKEND_KEEPALIVE);
}

static const struct cloud_api coap_backend_api = {
	.init			= c_init,
	.connect		= c_connect,
	.disconnect		= c_disconnect,
	.send			= c_send,
	.ping			= c_ping,
	.keepalive_time_left	= c_keepalive_time_left,
	.input			= c_input,
	.ep_subscriptions_add	= NULL,
};

CLOUD_BACKEND_DEFINE(COAP_BACKEND, coap_backend_api);
#endif
