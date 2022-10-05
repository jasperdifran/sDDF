/*
 * Copyright 2022, UNSW
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#define UDP_ECHO_PORT 1235
#define TCP_SERVER_PORT 80
#define UTILIZATION_PORT 1236
#define WEBSRV_CH 6

int setup_udp_socket(void);
int setup_utilization_socket(void);

int setup_tcp_socket(void);

int websrv_socket_send_response();
