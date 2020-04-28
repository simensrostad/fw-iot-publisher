#include <mqtt_backend.h>
#include <net/mqtt.h>
#include <net/socket.h>
#include <net/cloud.h>
#include <stdio.h>

#include <logging/log.h>

LOG_MODULE_REGISTER(mqtt_backend, CONFIG_MQTT_BACKEND_LOG_LEVEL);

BUILD_ASSERT_MSG(sizeof(CONFIG_MQTT_BACKEND_BROKER_HOST_NAME) > 1,
		 "MQTT Backend broker hostname not set");

#if defined(CONFIG_MQTT_BACKEND_IPV6)
#define MQTT_BACKEND_FAMILY AF_INET6
#else
#define MQTT_BACKEND_FAMILY AF_INET
#endif

#define MQTT_BACKEND_CLIENT_ID_PREFIX "%s"
#define MQTT_BACKEND_CLIENT_ID_LEN_MAX CONFIG_MQTT_BACKEND_CLIENT_ID_MAX_LEN

#define UPDATE_TOPIC "%s"
#define UPDATE_TOPIC_LEN (MQTT_BACKEND_CLIENT_ID_LEN_MAX)

static char client_id_buf[MQTT_BACKEND_CLIENT_ID_LEN_MAX + 1];
static char update_topic[UPDATE_TOPIC_LEN + 1];

static char rx_buffer[CONFIG_MQTT_BACKEND_MQTT_RX_TX_BUFFER_LEN];
static char tx_buffer[CONFIG_MQTT_BACKEND_MQTT_RX_TX_BUFFER_LEN];
static char payload_buf[CONFIG_MQTT_BACKEND_MQTT_PAYLOAD_BUFFER_LEN];

static struct mqtt_client client;
static struct sockaddr_storage broker;

#if !defined(CONFIG_CLOUD_API)
static mqtt_backend_evt_handler_t module_evt_handler;
#endif

#if defined(CONFIG_CLOUD_API)
static struct cloud_backend *mqtt_backend;
#endif

static int mqtt_backend_topics_populate(void)
{
	int err;

	err = snprintf(client_id_buf, sizeof(client_id_buf),
		       MQTT_BACKEND_CLIENT_ID_PREFIX,
		       CONFIG_MQTT_BACKEND_CLIENT_ID_STATIC);
	if (err >= MQTT_BACKEND_CLIENT_ID_LEN_MAX) {
		return -ENOMEM;
	}

	err = snprintf(update_topic, sizeof(update_topic),
		       UPDATE_TOPIC,
		       client_id_buf);
	if (err >= UPDATE_TOPIC_LEN) {
		return -ENOMEM;
	}

	return 0;
}

#if !defined(CONFIG_CLOUD_API)
static void mqtt_backend_notify_event(const struct mqtt_backend_evt *evt)
{
	if ((module_evt_handler != NULL) && (evt != NULL)) {
		module_evt_handler(evt);
	}
}
#endif

static int publish_get_payload(struct mqtt_client *const c, size_t length)
{
	if (length > sizeof(payload_buf)) {
		LOG_ERR("Incoming MQTT message too large for payload buffer");
		return -EMSGSIZE;
	}

	return mqtt_readall_publish_payload(c, payload_buf, length);
}

static void mqtt_evt_handler(struct mqtt_client *const c,
			     const struct mqtt_evt *mqtt_evt)
{
	int err;
#if defined(CONFIG_CLOUD_API)
	struct cloud_backend_config *config = mqtt_backend->config;
	struct cloud_event cloud_evt = { 0 };
#else
	struct mqtt_backend_evt mqtt_backend_evt = { 0 };
#endif

	switch (mqtt_evt->type) {
	case MQTT_EVT_CONNACK:
		LOG_DBG("MQTT client connected!");


		LOG_DBG("CONNACK, error: %d",
			mqtt_evt->param.connack.return_code);

#if defined(CONFIG_CLOUD_API)
		cloud_evt.type = CLOUD_EVT_CONNECTED;
		cloud_notify_event(mqtt_backend, &cloud_evt,
				   config->user_data);
		cloud_evt.type = CLOUD_EVT_READY;
		cloud_notify_event(mqtt_backend, &cloud_evt,
				   config->user_data);
#else
		mqtt_backend_evt.type = MQTT_BACKEND_EVT_CONNECTED;
		mqtt_backend_notify_event(&mqtt_backend_evt);
		mqtt_backend_evt.type = MQTT_BACKEND_EVT_READY;
		mqtt_backend_notify_event(&mqtt_backend_evt);
#endif
		break;
	case MQTT_EVT_DISCONNECT:
		LOG_DBG("MQTT_EVT_DISCONNECT: result = %d", mqtt_evt->result);

#if defined(CONFIG_CLOUD_API)
		cloud_evt.type = CLOUD_EVT_DISCONNECTED;
		cloud_notify_event(mqtt_backend, &cloud_evt,
				   config->user_data);
#else
		mqtt_backend_evt.type = MQTT_BACKEND_EVT_DISCONNECTED;
		mqtt_backend_notify_event(&mqtt_backend_evt);
#endif
		break;
	case MQTT_EVT_PUBLISH: {
		const struct mqtt_publish_param *p = &mqtt_evt->param.publish;

		LOG_DBG("MQTT_EVT_PUBLISH: id = %d len = %d ",
			p->message_id,
			p->message.payload.len);

		err = publish_get_payload(c, p->message.payload.len);
		if (err) {
			LOG_ERR("publish_get_payload, error: %d", err);
			break;
		}

		if (p->message.topic.qos == MQTT_QOS_1_AT_LEAST_ONCE) {
			const struct mqtt_puback_param ack = {
				.message_id = p->message_id
			};

			mqtt_publish_qos1_ack(c, &ack);
		}

#if defined(CONFIG_CLOUD_API)
		cloud_evt.type = CLOUD_EVT_DATA_RECEIVED;
		cloud_evt.data.msg.buf = payload_buf;
		cloud_evt.data.msg.len = p->message.payload.len;

		cloud_notify_event(mqtt_backend, &cloud_evt,
				   config->user_data);
#else
		mqtt_backend_evt.type = MQTT_BACKEND_DATA_RECEIVED;
		mqtt_backend_evt.ptr = payload_buf;
		mqtt_backend_evt.len = p->message.payload.len;
		mqtt_backend_notify_event(&mqtt_backend_evt);
#endif

	} break;
	case MQTT_EVT_PUBACK:
		LOG_DBG("MQTT_EVT_PUBACK: id = %d result = %d",
			mqtt_evt->param.puback.message_id,
			mqtt_evt->result);
		break;
	case MQTT_EVT_SUBACK:
		LOG_DBG("MQTT_EVT_SUBACK: id = %d result = %d",
			mqtt_evt->param.suback.message_id,
			mqtt_evt->result);
		break;
	default:
		break;
	}
}

static int broker_init(void)
{
	int err;
	struct addrinfo *result;
	struct addrinfo *addr;
	struct addrinfo hints = {
		.ai_family = MQTT_BACKEND_FAMILY,
		.ai_socktype = SOCK_STREAM
	};

	err = getaddrinfo(CONFIG_MQTT_BACKEND_BROKER_HOST_NAME,
			  NULL, &hints, &result);
	if (err) {
		LOG_ERR("getaddrinfo, error %d", err);
		return err;
	}

	addr = result;

	while (addr != NULL) {
		if ((addr->ai_addrlen == sizeof(struct sockaddr_in)) &&
		    (MQTT_BACKEND_FAMILY == AF_INET)) {
			struct sockaddr_in *broker4 =
				((struct sockaddr_in *)&broker);
			char ipv4_addr[NET_IPV4_ADDR_LEN];

			broker4->sin_addr.s_addr =
				((struct sockaddr_in *)addr->ai_addr)
				->sin_addr.s_addr;
			broker4->sin_family = AF_INET;
			broker4->sin_port = htons(CONFIG_MQTT_BACKEND_BROKER_PORT);

			inet_ntop(AF_INET, &broker4->sin_addr.s_addr, ipv4_addr,
				  sizeof(ipv4_addr));
			LOG_DBG("IPv4 Address found %s", log_strdup(ipv4_addr));
			break;
		} else if ((addr->ai_addrlen == sizeof(struct sockaddr_in6)) &&
			   (MQTT_BACKEND_FAMILY == AF_INET6)) {
			struct sockaddr_in6 *broker6 =
				((struct sockaddr_in6 *)&broker);
			char ipv6_addr[NET_IPV6_ADDR_LEN];

			memcpy(broker6->sin6_addr.s6_addr,
			       ((struct sockaddr_in6 *)addr->ai_addr)
			       ->sin6_addr.s6_addr,
			       sizeof(struct in6_addr));
			broker6->sin6_family = AF_INET6;
			broker6->sin6_port = htons(CONFIG_MQTT_BACKEND_BROKER_PORT);

			inet_ntop(AF_INET6, &broker6->sin6_addr.s6_addr,
				  ipv6_addr, sizeof(ipv6_addr));
			LOG_DBG("IPv4 Address found %s", log_strdup(ipv6_addr));
			break;
		}

		LOG_DBG("ai_addrlen = %u should be %u or %u",
			(unsigned int)addr->ai_addrlen,
			(unsigned int)sizeof(struct sockaddr_in),
			(unsigned int)sizeof(struct sockaddr_in6));

		addr = addr->ai_next;
		break;
	}

	freeaddrinfo(result);

	return err;
}

static int client_broker_init(struct mqtt_client *const client)
{
	int err;

	mqtt_client_init(client);

	err = broker_init();
	if (err) {
		return err;
	}

	client->broker			= &broker;
	client->evt_cb			= mqtt_evt_handler;
	client->client_id.utf8		= (char *)client_id_buf;
	client->client_id.size		= strlen(client_id_buf);
	client->password		= NULL;
	client->user_name		= NULL;
	client->protocol_version	= MQTT_VERSION_3_1_1;
	client->rx_buf			= rx_buffer;
	client->rx_buf_size		= sizeof(rx_buffer);
	client->tx_buf			= tx_buffer;
	client->tx_buf_size		= sizeof(tx_buffer);

#if defined(CONFIG_MQTT_BACKEND_TLS_ENABLE)
	client->transport.type		= MQTT_TRANSPORT_SECURE;

	static sec_tag_t sec_tag_list[] = { CONFIG_MQTT_BACKEND_SEC_TAG };
	struct mqtt_sec_config *tls_cfg = &(client->transport).tls.config;

	tls_cfg->peer_verify		= 2;
	tls_cfg->cipher_count		= 0;
	tls_cfg->cipher_list		= NULL;
	tls_cfg->sec_tag_count		= ARRAY_SIZE(sec_tag_list);
	tls_cfg->sec_tag_list		= sec_tag_list;
	tls_cfg->hostname		= CONFIG_MQTT_BACKEND_BROKER_HOST_NAME;
#else
	client->transport.type		= MQTT_TRANSPORT_NON_SECURE;
#endif
	return err;
}

int mqtt_backend_ping(void)
{
	return mqtt_ping(&client);
}

int mqtt_backend_keepalive_time_left(void)
{
	return (int)mqtt_keepalive_time_left(&client);
}

int mqtt_backend_input(void)
{
	return mqtt_input(&client);
}

int mqtt_backend_send(const struct mqtt_backend_tx_data *const tx_data)
{
	struct mqtt_backend_tx_data tx_data_pub = {
		.str	    = tx_data->str,
		.len	    = tx_data->len,
		.qos	    = tx_data->qos,
		.topic.type = tx_data->topic.type,
		.topic.str  = tx_data->topic.str,
		.topic.len  = tx_data->topic.len
	};

#if !defined(CONFIG_CLOUD_API)
	switch (tx_data->topic.type) {
	case MQTT_BACKEND_TOPIC_MSG:
		tx_data_pub.topic.str = update_topic;
		tx_data_pub.topic.len = strlen(update_topic);
		break;
	default:
		LOG_ERR("No endpoint topic available");
		break;
	}
#endif

	struct mqtt_publish_param param;

	param.message.topic.qos		= tx_data_pub.qos;
	param.message.topic.topic.utf8	= tx_data_pub.topic.str;
	param.message.topic.topic.size	= tx_data_pub.topic.len;
	param.message.payload.data	= tx_data_pub.str;
	param.message.payload.len	= tx_data_pub.len;
	param.message_id		= sys_rand32_get();
	param.dup_flag			= 0;
	param.retain_flag		= 0;

	LOG_DBG("Publishing to topic: %s",
		log_strdup(param.message.topic.topic.utf8));

	return mqtt_publish(&client, &param);
}

int mqtt_backend_disconnect(void)
{
	return mqtt_disconnect(&client);
}

int mqtt_backend_connect(struct mqtt_backend_config *const config)
{
	int err;

	err = client_broker_init(&client);
	if (err) {
		LOG_ERR("client_broker_init, error: %d", err);
		return err;
	}

	err = mqtt_connect(&client);
	if (err) {
		LOG_ERR("mqtt_connect, error: %d", err);
		return err;
	}

#if !defined(CONFIG_CLOUD_API)
#if defined(CONFIG_MQTT_BACKEND_TLS_ENABLE)
	config->socket = client.transport.tls.sock;
#else
	config->socket = client.transport.tcp.sock;
#endif
#endif

	return err;
}

int mqtt_backend_init(const struct mqtt_backend_config *const config,
		 mqtt_backend_evt_handler_t event_handler)
{
	int err;

	err = mqtt_backend_topics_populate();
	if (err) {
		LOG_ERR("mqtt_backend_topics_populate, error: %d", err);
		return err;
	}

#if !defined(CONFIG_CLOUD_API)
	module_evt_handler = event_handler;
#endif

	return err;
}

#if defined(CONFIG_CLOUD_API)
static int c_init(const struct cloud_backend *const backend,
		  cloud_evt_handler_t handler)
{
	backend->config->handler = handler;
	mqtt_backend = (struct cloud_backend *)backend;

	struct mqtt_backend_config config = {
		.client_id = backend->config->id,
		.client_id_len = backend->config->id_len
	};

	return mqtt_backend_init(&config, NULL);
}

static int c_connect(const struct cloud_backend *const backend)
{
	int err;

	err = mqtt_backend_connect(NULL);
	if (err) {
		return err;
	}

#if defined(CONFIG_MQTT_BACKEND_TLS_ENABLE)
	backend->config->socket = client.transport.tls.sock;
#else
	backend->config->socket = client.transport.tcp.sock;
#endif
	return err;
}

static int c_disconnect(const struct cloud_backend *const backend)
{
	return mqtt_backend_disconnect();
}

static int c_send(const struct cloud_backend *const backend,
		  const struct cloud_msg *const msg)
{
	struct mqtt_backend_tx_data tx_data = {
		.str = msg->buf,
		.len = msg->len,
		.qos = msg->qos
	};

	switch (msg->endpoint.type) {
	case CLOUD_EP_TOPIC_MSG:
		tx_data.topic.str = update_topic;
		tx_data.topic.len = strlen(update_topic);
	default:
		LOG_ERR("No endpoint topic available");
		break;
	}

	return mqtt_backend_send(&tx_data);
}

static int c_input(const struct cloud_backend *const backend)
{
	return mqtt_backend_input();
}

static int c_ping(const struct cloud_backend *const backend)
{
	return mqtt_backend_ping();
}

static int c_keepalive_time_left(const struct cloud_backend *const backend)
{
	return mqtt_backend_keepalive_time_left();
}

static const struct cloud_api mqtt_backend_api = {
	.init			= c_init,
	.connect		= c_connect,
	.disconnect		= c_disconnect,
	.send			= c_send,
	.ping			= c_ping,
	.keepalive_time_left	= c_keepalive_time_left,
	.input			= c_input
};

CLOUD_BACKEND_DEFINE(MQTT_BACKEND, mqtt_backend_api);
#endif
