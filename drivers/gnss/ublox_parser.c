/*
 * ublox_parser.c
 * Copyright (C) 2020 xent
 * Project is distributed under the terms of the MIT License
 */

#include <dpm/drivers/gnss/ublox_parser.h>
#include <string.h>
/*----------------------------------------------------------------------------*/
#define UBLOX_SYNC_WORD_1 0xB5
#define UBLOX_SYNC_WORD_2 0x62

enum State
{
  STATE_SYNC,
  STATE_TYPE,
  STATE_LENGTH,
  STATE_PAYLOAD,
  STATE_CHECKSUM,
  STATE_DONE
};
/*----------------------------------------------------------------------------*/
static void resetParserState(struct UbloxParser *);
static void updateChecksumWithByte(uint8_t *, uint8_t);
static void updateChecksumWithBuffer(uint8_t *, const uint8_t *, size_t);
/*----------------------------------------------------------------------------*/
static void resetParserState(struct UbloxParser *parser)
{
  parser->state = STATE_SYNC;
  parser->position = 0;

  parser->message.length = 0;
  parser->message.type = 0;
}
/*----------------------------------------------------------------------------*/
static void updateChecksumWithByte(uint8_t *result, uint8_t value)
{
  result[0] = result[0] + value;
  result[1] = result[1] + result[0];
}
/*----------------------------------------------------------------------------*/
static void updateChecksumWithBuffer(uint8_t *result, const uint8_t *values,
    size_t length)
{
  while (length--)
  {
    result[0] = result[0] + *values++;
    result[1] = result[1] + result[0];
  }
}
/*----------------------------------------------------------------------------*/
void ubloxParserInit(struct UbloxParser *parser)
{
  resetParserState(parser);
}
/*----------------------------------------------------------------------------*/
const struct UbloxMessage *ubloxParserData(const struct UbloxParser *parser)
{
  return &parser->message;
}
/*----------------------------------------------------------------------------*/
size_t ubloxParserPrepare(uint8_t *buffer, size_t length,
    const struct UbloxMessage *message)
{
  static const size_t LENGTH_OVERHEAD = 8;

  if (length < message->length + LENGTH_OVERHEAD)
    return 0;

  uint8_t *position = buffer;
  uint8_t checksum[2] = {0};

  *position++ = UBLOX_SYNC_WORD_1;
  *position++ = UBLOX_SYNC_WORD_2;
  *position++ = (uint8_t)message->type;
  *position++ = (uint8_t)(message->type >> 8);
  *position++ = (uint8_t)message->length;
  *position++ = (uint8_t)(message->length >> 8);

  memcpy(position, message->data, message->length);
  position += message->length;

  updateChecksumWithBuffer(checksum, buffer + 2, position - buffer - 2);
  *position++ = checksum[0];
  *position++ = checksum[1];

  return position - buffer;
}
/*----------------------------------------------------------------------------*/
size_t ubloxParserProcess(struct UbloxParser *parser, const uint8_t *buffer,
    size_t length)
{
  size_t i;

  if (parser->state == STATE_DONE)
    resetParserState(parser);

  for (i = 0; i < length; ++i)
  {
    const uint8_t c = buffer[i];

    switch (parser->state)
    {
      case STATE_SYNC:
        if (parser->position == 0)
        {
          if (c == UBLOX_SYNC_WORD_1)
            parser->position = 1;
        }
        else
        {
          if (c == UBLOX_SYNC_WORD_2)
          {
            parser->checksum[0] = 0;
            parser->checksum[1] = 0;
            parser->state = STATE_TYPE;
          }

          parser->position = 0;
        }
        break;

      case STATE_TYPE:
        if (parser->position == 0)
        {
          parser->message.type = (uint16_t)c;
          parser->position = 1;
        }
        else
        {
          parser->message.type |= (uint16_t)(c << 8);
          parser->position = 0;
          parser->state = STATE_LENGTH;
        }

        updateChecksumWithByte(parser->checksum, c);
        break;

      case STATE_LENGTH:
        if (parser->position == 0)
        {
          parser->message.length = (uint16_t)c;
          parser->position = 1;
        }
        else
        {
          parser->message.length |= (uint16_t)(c << 8);
          parser->position = 0;
        	parser->state = parser->message.length ?
              STATE_PAYLOAD : STATE_CHECKSUM;
        }

        updateChecksumWithByte(parser->checksum, c);
        break;

      case STATE_PAYLOAD:
        if (parser->message.length <= UBLOX_MESSAGE_LENGTH)
          parser->message.data[parser->position] = c;

        if (++parser->position == parser->message.length)
        {
          parser->state = STATE_CHECKSUM;
          parser->position = 0;
        }

        updateChecksumWithByte(parser->checksum, c);
        break;

      case STATE_CHECKSUM:
        if (parser->checksum[parser->position] == c)
        {
          if (++parser->position == 2)
          {
            if (parser->message.length <= UBLOX_MESSAGE_LENGTH)
              parser->state = STATE_DONE;
            else
              resetParserState(parser);
          }
        }
        else
        {
          resetParserState(parser);
        }
        break;

      case STATE_DONE:
        return i;
    }
  }

  return i;
}
/*----------------------------------------------------------------------------*/
bool ubloxParserReady(const struct UbloxParser *parser)
{
  return parser->state == STATE_DONE;
}
/*----------------------------------------------------------------------------*/
void ubloxParserReset(struct UbloxParser *parser)
{
  resetParserState(parser);
}
