// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "pebble.h"

#include "generate.h"
#include "unixtime.h"

#define P_UTCOFFSET       1
#define P_TOKENS_COUNT    2
#define P_SELECTED_LIST_INDEX    3
#define P_TOKENS_START    10000

#define MAX_NAME_LENGTH   32

Window *window;

typedef enum PersistenceWritebackFlags {
  PWNone = 0,
  PWUTCOffset = 1,
  PWTokens = 1 << 1
} PersistenceWritebackFlags;

PersistenceWritebackFlags persist_writeback = PWNone;

typedef enum AMKey {
  AMSetUTCOffset = 0, // Int32 with offset

  AMCreateToken = 1, // UInt8 array with secret
  AMCreateToken_ID = 2, // Short with ID for token (provided by phone)
  AMCreateToken_Name = 3, // Char array with name for token (provided by phone)

  AMDeleteToken = 4, // Short with token ID
  AMClearTokens = 5,

  AMReadTokenList = 6, // Starts token list read
  AMReadTokenList_Result = 7, // Struct with token info, returned in order of the list

  AMUpdateToken = 8, // Struct with token info

  AMSetTokenListOrder = 9 // array of shorts of token IDs
} AMKey;

typedef struct TokenInfo {
  char name[MAX_NAME_LENGTH + 1];
  short id;
  uint8_t secret[TOTP_SECRET_SIZE];
  char code[7];
} TokenInfo;

typedef struct PublicTokenInfo {
  short id;
  char name[MAX_NAME_LENGTH + 1];
} PublicTokenInfo;

typedef struct TokenListNode {
  struct TokenListNode* next;
  TokenInfo* key;
} TokenListNode;

TokenListNode* token_list = NULL;
bool key_list_is_dirty = false;

Layer *bar_layer;

TextLayer *no_tokens_layer;

MenuLayer *code_list_layer;

int utc_offset;

int token_list_retrieve_index = 0;

int startup_selected_list_index = 0;

void token_list_add(TokenInfo* key) {
  TokenListNode* node = malloc(sizeof(TokenListNode));
  node->next = NULL;
  node->key = key;

  if (!token_list) {
    token_list = node;
  } else {
    TokenListNode* tail = token_list;
    while (tail->next != NULL) {
      tail = tail->next;
    }
    tail->next = node;
  }
  key_list_is_dirty = true;
}

TokenInfo* token_by_list_index(int index) {
  TokenListNode* node = token_list;
  for (int i = 0; i < index; ++i)
  {
    node = node->next;
  }
  return node->key;
}

TokenInfo* token_by_id(short id) {
  TokenListNode* node = token_list;
  while (node) {
    if (node->key->id == id) {
      return node->key;
    }
    node = node->next;
  }
  return NULL;
}

short token_list_length(void){
  short size = 0;
  TokenListNode* node = token_list;
  while (node) {
    node = node->next;
    size++;
  }
  return size;
}

void token_list_clear(void){
  TokenListNode* temp;
  while (token_list) {
    temp = token_list;
    token_list = temp->next;
    free(temp->key); // Since it'd be a pain to do this otherwise.
    free(temp);
  }
  key_list_is_dirty = true;
}

bool token_list_delete(TokenInfo* key){
  TokenListNode* node = token_list;
  TokenListNode* last = NULL;
  while (node && node->key != key) {
    last = node;
    node = node->next;
  }
  if (node) {
    if (last) {
      last->next = node->next;
    } else {
      token_list = NULL;
    }
    free(node);
    key_list_is_dirty = true;
    return true;
  }
  return false;
}

void token_list_supplant(TokenListNode* newList) {
  TokenListNode* last = NULL;
  // Free the existing list nodes (but not their TokenInfos)
  while (token_list) {
    last = token_list;
    token_list = last->next;
    free(last);
  }
  token_list = newList;
  key_list_is_dirty = true;
}

void tokeninfo2publicinfo(TokenInfo* key, PublicTokenInfo* public) {
  public->id = key->id;
  strncpy(public->name, key->name, MAX_NAME_LENGTH);
  public->name[MAX_NAME_LENGTH] = 0;
}

void publicinfo2tokeninfo(PublicTokenInfo* public, TokenInfo* key) {
  key->id = public->id;
  strncpy(key->name, public->name, MAX_NAME_LENGTH);
  key->name[MAX_NAME_LENGTH] = 0;
}

void code2char(unsigned int code, char* out) {
  for(int x=0; x<6; x++) {
    out[5-x] = '0' + (code % 10);
    code /= 10;
  }
}

void show_no_tokens_message(bool show) {
  layer_set_hidden((Layer*)code_list_layer, show);
  layer_set_hidden(bar_layer, show);
  layer_set_hidden((Layer*)no_tokens_layer, !show);
}

void refresh_all(void){
  static unsigned int lastQuantizedTimeGenerated = 0;

  unsigned long utcTime = time(NULL) - utc_offset;

  unsigned int quantized_time = utcTime/30;

  if (quantized_time == lastQuantizedTimeGenerated && !key_list_is_dirty) {
    return;
  }

  key_list_is_dirty = false;

  bool hasKeys = false;

  lastQuantizedTimeGenerated = quantized_time;

  TokenListNode* keyNode = token_list;
  while (keyNode) {
    unsigned int code = generateCode(keyNode->key->secret, quantized_time);
    code2char(code, (char*)&keyNode->key->code);
    keyNode = keyNode->next;
    hasKeys = true;
  }

  if (hasKeys) {
    menu_layer_reload_data(code_list_layer);
  }
  show_no_tokens_message(!hasKeys);
}

void bar_layer_update(Layer *l, GContext* ctx) {
  graphics_context_set_fill_color(ctx, GColorBlack);
  unsigned short slice = 30 - (time(NULL) % 30);
  graphics_fill_rect(ctx, GRect(0, 0, (slice * 48) / 10, 5), 0, GCornerNone);
}

void draw_code_row(GContext *ctx, const Layer *cell_layer, MenuIndex *cell_index, void *callback_context){
  graphics_context_set_text_color(ctx, GColorBlack);
  TokenInfo* key = token_by_list_index(cell_index->row);
  graphics_draw_text(ctx, (char*)key->name, fonts_get_system_font(FONT_KEY_GOTHIC_14), GRect(0, 36, 144, 20), GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
  graphics_draw_text(ctx, (char*)key->code, fonts_get_system_font(FONT_KEY_BITHAM_34_MEDIUM_NUMBERS), GRect(0, 0, 144, 100), GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
}

uint16_t num_code_rows(struct MenuLayer *menu_layer, uint16_t section_index, void *callback_context){
  return token_list_length();
}

int16_t get_cell_height(struct MenuLayer *menu_layer, MenuIndex *cell_index, void *callback_context) {
  return 55;
}

void token_list_retrieve_iter(void) {
  if (token_list_retrieve_index == token_list_length()) return;

  TokenInfo* key = token_by_list_index(token_list_retrieve_index);
  PublicTokenInfo* public = malloc(sizeof(PublicTokenInfo));
  DictionaryIterator *iter;
  app_message_outbox_begin(&iter);

  tokeninfo2publicinfo(key, public);
  Tuplet record = TupletBytes(AMReadTokenList_Result, (uint8_t*)public, sizeof(PublicTokenInfo));
  dict_write_tuplet(iter, &record);
  app_message_outbox_send();

  token_list_retrieve_index++;
  free(public);
}

void in_received_handler(DictionaryIterator *received, void *context) {
  static bool delta = false;
  Tuple *utcoffset_tuple = dict_find(received, AMSetUTCOffset);
  if (utcoffset_tuple) {
    if (utc_offset != utcoffset_tuple->value->int32){
      delta = true;
      persist_writeback |= PWUTCOffset;
    }

    utc_offset = utcoffset_tuple->value->int32;
    APP_LOG(APP_LOG_LEVEL_DEBUG, "Set TZ offset %d", utc_offset);
   }

  if (dict_find(received, AMClearTokens)) {
    APP_LOG(APP_LOG_LEVEL_DEBUG, "Clear tokens");
    token_list_clear();

    persist_writeback |= PWTokens;
    delta = true;
  }

  Tuple *delete_token = dict_find(received, AMDeleteToken);
  if (delete_token) {
    APP_LOG(APP_LOG_LEVEL_DEBUG, "Delete token %d", delete_token->value->int8);
    TokenInfo* key = token_by_id(delete_token->value->int8);
    token_list_delete(key);
    free(key);

    persist_writeback |= PWTokens;
    delta = true;
  }

  Tuple *update_token = dict_find(received, AMUpdateToken);
  if (update_token) {
    PublicTokenInfo* public = (PublicTokenInfo*)&update_token->value->data;
    APP_LOG(APP_LOG_LEVEL_DEBUG, "Update token %d - %s", public->id, public->name);
    publicinfo2tokeninfo(public, token_by_id(public->id));

    persist_writeback |= PWTokens;
    delta = true;
  }

  Tuple *create_token = dict_find(received, AMCreateToken);
  if (create_token) {
    uint8_t* secret = create_token->value->data;
    TokenInfo* newKey = malloc(sizeof(TokenInfo));
    memcpy((char*)&newKey->secret, secret, TOTP_SECRET_SIZE);
    newKey->id = dict_find(received, AMCreateToken_ID)->value->int32;
    strncpy((char*)&newKey->name, dict_find(received, AMCreateToken_Name)->value->cstring, MAX_NAME_LENGTH);
    newKey->name[MAX_NAME_LENGTH] = 0;

    token_list_add(newKey);
    APP_LOG(APP_LOG_LEVEL_DEBUG, "Create token %d - %s", newKey->id, newKey->name);

    persist_writeback |= PWTokens;
    delta = true;
  }

  if (dict_find(received, AMReadTokenList)) {
    APP_LOG(APP_LOG_LEVEL_DEBUG, "Listing tokens");
    token_list_retrieve_index = 0;
    token_list_retrieve_iter();
  }

  Tuple *reorder_list = dict_find(received, AMSetTokenListOrder);
  if (reorder_list) {
    APP_LOG(APP_LOG_LEVEL_DEBUG, "Reordering tokens");
    TokenListNode* newList = NULL;
    TokenListNode* node = NULL;
    TokenListNode* last = NULL;

    // Build a new list using the existing TokenInfos
    int ct = token_list_length();
    for (int i = 0; i < ct; ++i)
    {
      node = malloc(sizeof(TokenListNode));
      if (last) {
        last->next = node;
      } else {
        newList = node;
      }
      node->next = NULL;
      node->key = token_by_id(reorder_list->value->data[i]);
      last = node;
    }

    // Free the existing list and replace.
    token_list_supplant(newList);

    persist_writeback |= PWTokens;
    delta = true;
  }

  if (delta){
    refresh_all();
  }
}

void out_sent_handler(DictionaryIterator *sent, void *context) {
  if (dict_find(sent, AMReadTokenList_Result)) {
    token_list_retrieve_iter();
  }
}

// Standard app init

void handle_init() {

  app_message_register_inbox_received(in_received_handler);
  app_message_register_outbox_sent(out_sent_handler);

  const uint32_t inbound_size = 64;
  const uint32_t outbound_size = 64;
  app_message_open(inbound_size, outbound_size);

  // Load persisted data
  utc_offset = persist_exists(P_UTCOFFSET) ? persist_read_int(P_UTCOFFSET) : 0;
  if (persist_exists(P_TOKENS_COUNT)) {
    int ct = persist_read_int(P_TOKENS_COUNT);
    APP_LOG(APP_LOG_LEVEL_INFO, "Starting with %d tokens", ct);
    for (int i = 0; i < ct; ++i) {
      TokenInfo* key = malloc(sizeof(TokenInfo));
      persist_read_data(P_TOKENS_START + i, key, sizeof(TokenInfo));
      token_list_add(key);
    }
  }

  window = window_create();
  window_stack_push(window, true /* Animated */);

  Layer* rootLayer = window_get_root_layer(window);
  GRect rootLayerRect = layer_get_bounds(rootLayer);
  bar_layer = layer_create(GRect(0,rootLayerRect.size.h-5,rootLayerRect.size.w,5));
  layer_set_update_proc(bar_layer, bar_layer_update);
  layer_add_child(rootLayer, bar_layer);

  no_tokens_layer = text_layer_create(GRect(0, rootLayerRect.size.h/2-35, rootLayerRect.size.w, 30*2));
  text_layer_set_text(no_tokens_layer, "No Tokens");
  text_layer_set_font(no_tokens_layer, fonts_get_system_font(FONT_KEY_BITHAM_30_BLACK));
  text_layer_set_text_color(no_tokens_layer, GColorBlack);
  text_layer_set_text_alignment(no_tokens_layer, GTextAlignmentCenter);
  layer_add_child(rootLayer, (Layer*)no_tokens_layer);

  code_list_layer = menu_layer_create(GRect(0,0,rootLayerRect.size.w, rootLayerRect.size.h - 4));

  MenuLayerCallbacks menuCallbacks = {
    .draw_row = draw_code_row,
    .get_num_rows = num_code_rows,
    .get_cell_height = get_cell_height
  };

  menu_layer_set_callbacks(code_list_layer, NULL, menuCallbacks);


  menu_layer_set_click_config_onto_window(code_list_layer, window);


  layer_add_child(rootLayer, (Layer*)code_list_layer);

  // Ideally we'd set this before we register the callbacks, so we wouldn't catch the change event should it be called.
  if (persist_exists(P_SELECTED_LIST_INDEX)) {
    MenuIndex index = {.row = persist_read_int(P_SELECTED_LIST_INDEX), .section=0};
    startup_selected_list_index = index.row;
    menu_layer_set_selected_index(code_list_layer, index, MenuRowAlignCenter, false);
  }


  refresh_all();
}

void handle_tick(struct tm* tick_time, TimeUnits units_changed) {
  refresh_all();
  layer_mark_dirty(bar_layer);
}

void handle_deinit() {
  if ((persist_writeback & PWUTCOffset) == PWUTCOffset) {
    // Write back persistent things
    persist_write_int(P_UTCOFFSET, utc_offset);
  }

  if (startup_selected_list_index != menu_layer_get_selected_index(code_list_layer).row) {
    persist_write_int(P_SELECTED_LIST_INDEX, menu_layer_get_selected_index(code_list_layer).row);
    APP_LOG(APP_LOG_LEVEL_DEBUG, "Wrote list index");
  }

  if ((persist_writeback & PWTokens) == PWTokens) {
    persist_write_int(P_TOKENS_COUNT, token_list_length());

    TokenListNode* node = token_list;
    short idx = 0;
    while (node) {
      persist_write_data(P_TOKENS_START + idx, node->key, sizeof(TokenInfo));
      idx++;
      node = node->next;
    }

    APP_LOG(APP_LOG_LEVEL_INFO, "Wrote %d tokens", idx);
  }

  token_list_clear();
  menu_layer_destroy(code_list_layer);
  layer_destroy(bar_layer);
  text_layer_destroy(no_tokens_layer);
  window_destroy(window);
}

int main() {
  handle_init();
  tick_timer_service_subscribe(SECOND_UNIT, handle_tick);
  app_event_loop();
  handle_deinit();
}
