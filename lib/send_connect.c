/*
Copyright (c) 2009-2016 Roger Light <roger@atchoo.org>

All rights reserved. This program and the accompanying materials
are made available under the terms of the Eclipse Public License v1.0
and Eclipse Distribution License v1.0 which accompany this distribution.

The Eclipse Public License is available at
   http://www.eclipse.org/legal/epl-v10.html
and the Eclipse Distribution License is available at
  http://www.eclipse.org/org/documents/edl-v10.php.

Contributors:
   Roger Light - initial implementation and documentation.
*/

#include <assert.h>
#include <string.h>

#include "logging_mosq.h"
#include "memory_mosq.h"
#include "mosquitto.h"
#include "mosquitto_internal.h"
#include "mqtt3_protocol.h"
#include "packet_mosq.h"

#ifdef WITH_BROKER
#  include "mosquitto_broker_internal.h"
#endif

int send__connect(struct mosquitto *mosq, uint16_t keepalive, bool clean_session)
{
	struct mosquitto__packet *packet = NULL;
	int payloadlen;
	uint8_t will = 0;
	uint8_t byte;
	int rc;
	uint8_t version;
	char *clientid, *username, *password;
	int headerlen;
#ifdef WITH_CLUSTER
	time_t now = mosquitto_time();
	char tmp_time_str[9] = {0};
#endif

	assert(mosq);
	assert(mosq->id);

#if defined(WITH_BROKER) && (defined(WITH_BRIDGE)||defined(WITH_CLUSTER))
#ifdef WITH_BRIDGE
	if(mosq->bridge){
		clientid = mosq->bridge->remote_clientid;
		username = mosq->bridge->remote_username;
		password = mosq->bridge->remote_password;
	}else{
		clientid = mosq->id;
		username = mosq->username;
		password = mosq->password;
	}
#endif
#ifdef WITH_CLUSTER
	if(mosq->is_node){
		clientid = mosq->node->remote_clientid;
		username = mosq->node->remote_username;
		password = mosq->node->remote_password;
	}else{
		clientid = mosq->id;
		username = mosq->username;
		password = mosq->password;
	}
#endif
#else
	clientid = mosq->id;
	username = mosq->username;
	password = mosq->password;
#endif

	if(mosq->protocol == mosq_p_mqtt31){
		version = MQTT_PROTOCOL_V31;
		headerlen = 12;
	}else if(mosq->protocol == mosq_p_mqtt311){
		version = MQTT_PROTOCOL_V311;
		headerlen = 10;
	}else{
		return MOSQ_ERR_INVAL;
	}

	packet = mosquitto__calloc(1, sizeof(struct mosquitto__packet));
	if(!packet) return MOSQ_ERR_NOMEM;

	payloadlen = 2+strlen(clientid);
	if(mosq->will){
		will = 1;
		assert(mosq->will->topic);

		payloadlen += 2+strlen(mosq->will->topic) + 2+mosq->will->payloadlen;
	}
	if(username){
		payloadlen += 2+strlen(username);
		if(password){
			payloadlen += 2+strlen(password);
		}
	}
#ifdef WITH_CLUSTER
	if(mosq->is_node)
		payloadlen += 10;
#endif
	packet->command = CONNECT;
	packet->remaining_length = headerlen+payloadlen;
	rc = packet__alloc(packet);
	if(rc){
		mosquitto__free(packet);
		return rc;
	}

	/* Variable header */
	if(version == MQTT_PROTOCOL_V31){
		packet__write_string(packet, PROTOCOL_NAME_v31, strlen(PROTOCOL_NAME_v31));
	}else if(version == MQTT_PROTOCOL_V311){
		packet__write_string(packet, PROTOCOL_NAME_v311, strlen(PROTOCOL_NAME_v311));
	}
#if defined(WITH_BROKER) && defined(WITH_BRIDGE)
	if(mosq->bridge && mosq->bridge->try_private && mosq->bridge->try_private_accepted){
		version |= 0x80;
	}else{
	}
#endif
#if defined(WITH_BROKER) && defined(WITH_CLUSTER)
	if(mosq->is_node){
		if(mosq->state == mosq_cs_new || mosq->state == mosq_cs_connect_pending){
			version |= MOSQ_NODE_MEET;
		}
	}
#endif

	packet__write_byte(packet, version);
	byte = (clean_session&0x1)<<1;
	if(will){
		byte = byte | ((mosq->will->retain&0x1)<<5) | ((mosq->will->qos&0x3)<<3) | ((will&0x1)<<2);
	}
	if(username){
		byte = byte | 0x1<<7;
		if(mosq->password){
			byte = byte | 0x1<<6;
		}
	}
	packet__write_byte(packet, byte);
	packet__write_uint16(packet, keepalive);

	/* Payload */
	packet__write_string(packet, clientid, strlen(clientid));
	if(will){
		packet__write_string(packet, mosq->will->topic, strlen(mosq->will->topic));
		packet__write_string(packet, (const char *)mosq->will->payload, mosq->will->payloadlen);
	}
	if(username){
		packet__write_string(packet, username, strlen(username));
		if(password){
			packet__write_string(packet, password, strlen(password));
		}
	}

	mosq->keepalive = keepalive;
#ifdef WITH_CLUSTER
	if(mosq->is_node){
		mosq_time_to_hexstr((int64_t)now, tmp_time_str);
		packet__write_string(packet, tmp_time_str, 8);
		log__printf(mosq, MOSQ_LOG_DEBUG, "[CLUSTER] send local time to node: %s, localtime: %ld", mosq->node->name, now);
	}
#endif
#ifdef WITH_BROKER
# ifdef WITH_BRIDGE
	log__printf(mosq, MOSQ_LOG_DEBUG, "Bridge %s sending CONNECT", clientid);
# endif
# ifdef WITH_CLUSTER
    if(mosq->node)
		log__printf(NULL, MOSQ_LOG_DEBUG, "[CLUSTER] Sending CONNECT to node: %s addr(%s:%d)", mosq->node->name, mosq->node->address, mosq->node->port);
# endif
#else
	log__printf(mosq, MOSQ_LOG_DEBUG, "Client %s sending CONNECT", clientid);
#endif
	return packet__queue(mosq, packet);
}

