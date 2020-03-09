/*
 * Copyright (c) 2020 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-BSD-5-Clause-Nordic
 */

/**@file
 *@brief CoAP library header.
 */

#ifndef COAP_BACKEND_H__
#define COAP_BACKEND_H__

#include <stdio.h>

/**
 * @defgroup CoAP library
 * @{
 * @brief Library to connect the device to a UDP server.
 */

#ifdef __cplusplus
extern "C" {
#endif

/** @brief CoAP notification event types, used to signal the application. */
enum coap_backend_evt_type {
	COAP_BACKEND_EVT_CONNECTED = 0x1,
	COAP_BACKEND_EVT_READY,
	COAP_BACKEND_EVT_DISCONNECTED,
	COAP_BACKEND_EVT_DATA_RECEIVED,
	COAP_BACKEND_EVT_ERROR,
	COAP_BACKEND_EVT_FOTA_DONE
};

/** @brief Struct with data received from UDP server. */
struct coap_backend_event {
	/** Type of event. */
	enum coap_backend_evt_type type;
	/** Pointer to data received from the UDP server. */
	char *ptr;
	/** Length of data. */
	size_t len;
};

/** @brief UDP backend transmission data. */
struct coap_backend_tx_data {
	/** Pointer to message to be sent to UDP server. */
	char *str;
	/** Length of message. */
	size_t len;
};

/** @brief CoAP library asynchronous event handler.
 *
 *  @param[in] evt The event and any associated parameters.
 */
typedef void (*coap_backend_evt_handler_t)(const struct coap_backend_event *evt);

/** @brief Structure for UDP server connection parameters. */
struct coap_backend_config {
	/** Socket for UDP server connection */
	int socket;
};

/** @brief Initialize the module.
 *
 *  @warning This API must be called exactly once, and it must return
 *           successfully.
 *
 *  @param[in] config Pointer to struct containing connection parameters.
 *  @param[in] event_handler Pointer to event handler to receive CoAP module
 *                           events.
 *
 *  @return 0 If successful.
 *            Otherwise, a (negative) error code is returned.
 */
int coap_backend_init(const struct coap_backend_config *const config,
		      coap_backend_evt_handler_t event_handler);

/** @brief Connect to the UDP server.
 *
 *  @details This function exposes the UDP socket to main so that it can be
 *           polled on.
 *
 *  @param[out] config Pointer to struct containing connection parameters,
 *                     the UDP connection socket number will be copied to the
 *                     socket entry of the struct.
 *
 *  @return 0 If successful.
 *            Otherwise, a (negative) error code is returned.
 */
int coap_backend_connect(struct coap_backend_config *const config);

/** @brief Disconnect from the UDP server.
 *
 *  @return 0 If successful.
 *            Otherwise, a (negative) error code is returned.
 */
int coap_backend_disconnect(void);

/** @brief Send data to UDP server.
 *
 *  @param[in] tx_data Pointer to struct containing data to be transmitted to
 *                     the UDP server.
 *
 *  @return 0 If successful.
 *            Otherwise, a (negative) error code is returned.
 */
int coap_backend_send(const struct coap_backend_tx_data *const tx_data);

/** @brief Get data from UDP server
 *
 *  @return 0 If successful.
 *            Otherwise, a (negative) error code is returned.
 */
int coap_backend_input(void);

/** @brief Ping UDP server. Must be called periodically
 *         to keep socket open.
 *
 *  @return 0 If successful.
 *            Otherwise, a (negative) error code is returned.
 */
int coap_backend_ping(void);


#ifdef __cplusplus
}
#endif

/**
 *@}
 */

#endif /* COAP_BACKEND_H__ */
