/*
 * Copyright (c) 2020 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-BSD-5-Clause-Nordic
 */

/**@file
 *@brief MQTT Backend library header.
 */

#ifndef MQTT_BACKEND_H__
#define MQTT_BACKEND_H__

#include <stdio.h>
#include <net/mqtt.h>

/**
 * @defgroup mqtt_backend MQTT Backend library
 * @{
 * @brief Library to connect a device to a MQTT Backend message broker.
 */

#ifdef __cplusplus
extern "C" {
#endif

/** @brief MQTT Backend topics, used in messages to specify which
 *         topic that will be published to.
 */
enum mqtt_backend_topic_type {
	MQTT_BACKEND_TOPIC_MSG = 0x1
};

/** @brief MQTT Backend notification event types, used to signal the application. */
enum mqtt_backend_evt_type {
	/** Connected to MQTT broker. **/
	MQTT_BACKEND_EVT_CONNECTED = 0x1,
	/** MQTT broker ready. */
	MQTT_BACKEND_EVT_READY,
	/** Disconnected from MQTT broker. */
	MQTT_BACKEND_EVT_DISCONNECTED,
	/** Data received from MQTT broker. */
	MQTT_BACKEND_EVT_DATA_RECEIVED,
	/** FOTA update done, request to reboot. */
	MQTT_BACKEND_EVT_FOTA_DONE
};

/** @brief Struct with data received from MQTT broker. */
struct mqtt_backend_evt {
	/** Type of event. */
	enum mqtt_backend_evt_type type;
	/** Pointer to data received from the MQTT broker. */
	char *ptr;
	/** Length of data. */
	size_t len;
};

/** @brief MQTT Backend topic data. */
struct mqtt_backend_topic_data {
	/** Type of topic that will be published to. */
	enum mqtt_backend_topic_type type;
	/** Pointer to string of application specific topic. */
	char *str;
	/** Length of application specific topic. */
	size_t len;
};

/** @brief MQTT Backend transmission data. */
struct mqtt_backend_tx_data {
	/** Topic that the message will be sent to. */
	struct mqtt_backend_topic_data topic;
	/** Pointer to message to be sent to the MQTT broker. */
	char *str;
	/** Length of message. */
	size_t len;
	/** Quality of Service of the message. */
	enum mqtt_qos qos;
};

/** @brief MQTT Backend library asynchronous event handler.
 *
 *  @param[in] evt The event and any associated parameters.
 */
typedef void (*mqtt_backend_evt_handler_t)(const struct mqtt_backend_evt *evt);

/** @brief Structure for MQTT Backend broker connection parameters. */
struct mqtt_backend_config {
	/** Socket for MQTT broker connection */
	int socket;
	/** Client id for MQTT broker connection. */
	char *client_id;
	/** Length of client_id string. */
	size_t client_id_len;
};

/** @brief Initialize the module.
 *
 *  @warning This API must be called exactly once, and it must return
 *           successfully.
 *
 *  @param[in] config Pointer to struct containing connection parameters.
 *  @param[in] event_handler Pointer to event handler to receive MQTT Backend
 *             module events.
 *
 *  @return 0 If successful.
 *            Otherwise, a (negative) error code is returned.
 */
int mqtt_backend_init(const struct mqtt_backend_config *const config,
		      mqtt_backend_evt_handler_t event_handler);

/** @brief Connect to the MQTT broker.
 *
 *  @details This function exposes the MQTT socket to main so that it can be
 *           polled on.
 *
 *  @param[out] config Pointer to struct containing the connection parameters,
 *                     the MQTT connection socket number will be copied to the
 *                     socket entry of the struct.
 *
 *  @return 0 If successful.
 *            Otherwise, a (negative) error code is returned.
 */
int mqtt_backend_connect(struct mqtt_backend_config *const config);

/** @brief Disconnect from the MQTT broker.
 *
 *  @return 0 If successful.
 *            Otherwise, a (negative) error code is returned.
 */
int mqtt_backend_disconnect(void);

/** @brief Send data to MQTT Backend broker.
 *
 *  @param[in] tx_data Pointer to a struct containing data to be transmitted to
 *                     the MQTT broker.
 *
 *  @return 0 If successful.
 *            Otherwise, a (negative) error code is returned.
 */
int mqtt_backend_send(const struct mqtt_backend_tx_data *const tx_data);

/** @brief Get data from MQTT broker.
 *
 *  @return 0 If successful.
 *            Otherwise, a (negative) error code is returned.
 */
int mqtt_backend_input(void);

/** @brief Ping the MQTT broker. Must be called periodically
 *         to keep connection to broker alive.
 *
 *  @return 0 If successful.
 *            Otherwise, a (negative) error code is returned.
 */
int mqtt_backend_ping(void);

#ifdef __cplusplus
}
#endif

/**
 *@}
 */

#endif /* AWS_IOT_H__ */
