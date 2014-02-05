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
#include "timezone.h"

#define P_UTCOFFSET   1
#define P_KEYCOUNT      2
#define P_KEYSTART   10000

Window *window;

typedef enum AMKey {
  AMUTCOffsetSet = 0, // Int32 with offset

  AMCreateCredential = 1, // Char array with secret
  AMCreateCredential_ID = 2, // Int with ID for credential (provided by phone)
  AMCreateCredential_Name = 3, // Char array with name for credential (provided by phone)

  AMDeleteCredential = 4, // Short with credential ID
  AMClearCredentials = 5,

  AMReadCredentialList = 6, // Starts credential read
  AMReadCredentialList_Result = 7, // Struct with credential info, returned in order of the list


  AMUpdateCredential = 8, // Struct with credential info

  AMSetCredentialListOrder = 9 // array of shorts of credential IDs
} AMKey;

typedef struct KeyInfo {
  char name[33];
  short id;
  char secret[33];
  char code[7];
} KeyInfo;

typedef struct PublicKeyInfo {
  short id;
  char name[33];
} PublicKeyInfo;

typedef struct KeyListNode {
  struct KeyListNode* next;
  KeyInfo* key;
} KeyListNode;

KeyInfo keys[0];
KeyListNode* keyList = NULL;
bool keyListDirty = false;
bool persistentStoreNeedsWriteback = false;

Layer *barLayer;

TextLayer *noTokensLayer;

MenuLayer *codeListLayer;

int utcOffset;

int key_list_read_index = 0;

void key_list_add(KeyInfo* key) {
  KeyListNode* node = malloc(sizeof(KeyListNode));
  node->next = NULL;
  node->key = key;

  if (!keyList) {
    keyList = node;
  } else {
    KeyListNode* tail = keyList;
    while (tail->next != NULL) {
      tail = tail->next;
    }
    tail->next = node;
  }
  keyListDirty = true;
}

KeyInfo* key_by_list_index(int index) {
  KeyListNode* node = keyList;
  for (int i = 0; i < index; ++i)
  {
    node = node->next;
  }
  return node->key;
}

KeyInfo* key_by_id(short id) {
  KeyListNode* node = keyList;
  while (node) {
    if (node->key->id == id) {
      return node->key;
    }
    node = node->next;
  }
  return NULL;
}

short key_list_length(void){
  short size = 0;
  KeyListNode* node = keyList;
  while (node) {
    node = node->next;
    size++;
  }
  return size;
}

void key_list_clear(void){
  KeyListNode* temp;
  while (keyList) {
    temp = keyList;
    keyList = temp->next;
    free(temp->key); // Since it'd be a pain to do this otherwise.
    free(temp);
  }
  keyListDirty = true;
}

bool key_list_delete(KeyInfo* key){
  KeyListNode* node = keyList;
  KeyListNode* last = NULL;
  while (node && node->key != key) {
    last = node;
    node = node->next;
  }
  if (node) {
    if (last) {
      last->next = node->next;
    } else {
      keyList = NULL;
    }
    free(node);
    keyListDirty = true;
    return true;
  }
  return false;
}

void keyinfo2publickeyinfo(KeyInfo* key, PublicKeyInfo* public) {
  public->id = key->id;
  strcpy(public->name, key->name);
}
void publickeyinfo2keyinfo(PublicKeyInfo* public, KeyInfo* key) {
  strcpy(key->name, public->name);
}

void code2char(unsigned int code, char* out) {
  for(int x=0;x<6;x++) {
    out[5-x] = '0'+(code % 10);
    code /= 10;
  }
}

void show_no_tokens_message(bool show) {
  layer_set_hidden((Layer*)codeListLayer, show);
  layer_set_hidden(barLayer, show);
  layer_set_hidden((Layer*)noTokensLayer, !show);
}

void refresh_all(void){
  static unsigned int lastQuantizedTimeGenerated = 0;

  unsigned long utcTime = time(NULL) - utcOffset;

  unsigned int quantized_time = utcTime/30;

  if (quantized_time == lastQuantizedTimeGenerated && !keyListDirty) {
    return;
  }

  keyListDirty = false;

  bool hasKeys = false;

  lastQuantizedTimeGenerated = quantized_time;

  KeyListNode* keyNode = keyList;
  while (keyNode) {
    unsigned int code = generateCode(keyNode->key->secret, quantized_time);
    code2char(code, (char*)&keyNode->key->code);
    keyNode = keyNode->next;
    hasKeys = true;
  }
  if (hasKeys) {
    menu_layer_reload_data(codeListLayer);
  }
  show_no_tokens_message(!hasKeys);

}


void bar_layer_update(Layer *l, GContext* ctx) {
  graphics_context_set_fill_color(ctx, GColorBlack);
  unsigned short slice = 30-(time(NULL)%30);
  graphics_fill_rect(ctx, GRect(0,0,(slice*48)/10,5), 0, GCornerNone);
}

void draw_code_row(GContext *ctx, const Layer *cell_layer, MenuIndex *cell_index, void *callback_context){
  graphics_context_set_text_color(ctx, GColorBlack);
  KeyInfo* key = key_by_list_index(cell_index->row);
  graphics_draw_text(ctx, (char*)key->name, fonts_get_system_font(FONT_KEY_GOTHIC_14), GRect(0, 36, 144, 20), GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
  graphics_draw_text(ctx, (char*)key->code, fonts_get_system_font(FONT_KEY_BITHAM_34_MEDIUM_NUMBERS), GRect(0, 0, 144, 100), GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
}

uint16_t num_code_rows(struct MenuLayer *menu_layer, uint16_t section_index, void *callback_context){
  return key_list_length();
}

int16_t get_cell_height(struct MenuLayer *menu_layer, MenuIndex *cell_index, void *callback_context) {
  return 55;
}

void key_list_read_iter(void) {
  if (key_list_read_index == key_list_length()) return;

  KeyInfo* key = key_by_list_index(key_list_read_index);
  PublicKeyInfo* public = malloc(sizeof(PublicKeyInfo));
  DictionaryIterator *iter;
  app_message_outbox_begin(&iter);

  APP_LOG(APP_LOG_LEVEL_INFO, "Listing credentials - iter %d", key_list_read_index);
  keyinfo2publickeyinfo(key, public);
  Tuplet record = TupletBytes(AMReadCredentialList_Result, (uint8_t*)public, sizeof(PublicKeyInfo));
  dict_write_tuplet(iter, &record);
  app_message_outbox_send();

  key_list_read_index++;
  free(public);
}

void in_received_handler(DictionaryIterator *received, void *context) {
  static bool delta = false;
  Tuple *utcoffset_tuple = dict_find(received, AMUTCOffsetSet);
  if (utcoffset_tuple) {
    utcOffset = utcoffset_tuple->value->int32;
    delta = true;
    APP_LOG(APP_LOG_LEVEL_INFO, "Set TZ %d", utcOffset);
   }

  if (dict_find(received, AMClearCredentials)) {
    APP_LOG(APP_LOG_LEVEL_INFO, "Clear credentials");
    key_list_clear();
    delta = true;
  }

  Tuple *delete_credential = dict_find(received, AMDeleteCredential);
  if (delete_credential) {
    APP_LOG(APP_LOG_LEVEL_INFO, "Delete credential %d", delete_credential->value->int8);
    KeyInfo* key = key_by_id(delete_credential->value->int8);
    key_list_delete(key);
    free(key);
    delta = true;
  }

  Tuple *update_credential = dict_find(received, AMUpdateCredential);
  if (update_credential) {
    PublicKeyInfo* public = (PublicKeyInfo*)&update_credential->value->data;
    APP_LOG(APP_LOG_LEVEL_INFO, "Update credential %d - %s", public->id, public->name);
    publickeyinfo2keyinfo(public, key_by_id(public->id));
    delta = true;
  }

  Tuple *create_credential = dict_find(received, AMCreateCredential);
  if (create_credential) {
    char* secret = create_credential->value->cstring;
    KeyInfo* newKey = malloc(sizeof(KeyInfo));
    strcpy((char*)&newKey->secret, secret);
    newKey->id = dict_find(received, AMCreateCredential_ID)->value->int32;
    strcpy((char*)&newKey->name, dict_find(received, AMCreateCredential_Name)->value->cstring);

    key_list_add(newKey);
    APP_LOG(APP_LOG_LEVEL_INFO, "Create credential %s", secret);
    delta = true;
  }
  if (dict_find(received, AMReadCredentialList)) {
    APP_LOG(APP_LOG_LEVEL_INFO, "Listing credentials");
    key_list_read_index = 0;
    key_list_read_iter();
  }

  Tuple *reorder_list = dict_find(received, AMSetCredentialListOrder);
  if (reorder_list) {
    KeyListNode* newList = NULL;
    KeyListNode* node = NULL;
    KeyListNode* last = NULL;
    int ct = key_list_length();
    for (int i = 0; i < ct; ++i)
    {
      // Build a new list using the existing KeyInfos
      node = malloc(sizeof(KeyListNode));
      if (last) {
        last->next = node;
      } else {
        newList = node;
      }
      node->next = NULL;
      node->key = key_by_id(reorder_list->value->data[i]);
      last = node;
    }
    // Free the existing list.
    last = NULL;
    while (keyList) {
      last = keyList;
      keyList = last->next;
      free(last);
    }
    keyList = newList;
    delta = true;
    keyListDirty = true;
  }

  persistentStoreNeedsWriteback = persistentStoreNeedsWriteback || delta;
  if (delta){
    refresh_all();
  }
}

void out_sent_handler(DictionaryIterator *sent, void *context) {
  APP_LOG(APP_LOG_LEVEL_INFO, "Sent message");
  if (dict_find(sent, AMReadCredentialList_Result)) {
    key_list_read_iter();
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
  utcOffset = persist_exists(P_UTCOFFSET) ? persist_read_int(P_UTCOFFSET) : 0;
  if (persist_exists(P_KEYCOUNT)) {
    int ct = persist_read_int(P_KEYCOUNT);
    APP_LOG(APP_LOG_LEVEL_INFO, "Starting with %d keys", P_KEYCOUNT);
    for (int i = 0; i < ct; ++i) {
      KeyInfo* key = malloc(sizeof(KeyInfo));
      persist_read_data(P_KEYSTART + i, key, sizeof(KeyInfo));
      key_list_add(key);
    }
  }

  window = window_create();
  window_stack_push(window, true /* Animated */);

  Layer* rootLayer = window_get_root_layer(window);
  GRect rootLayerRect = layer_get_bounds(rootLayer);
  barLayer = layer_create(GRect(0,rootLayerRect.size.h-5,rootLayerRect.size.w,5));
  layer_set_update_proc(barLayer, bar_layer_update);
  layer_add_child(rootLayer, barLayer);

  noTokensLayer = text_layer_create(GRect(0, rootLayerRect.size.h/2-35, rootLayerRect.size.w, 30*2));
  text_layer_set_text(noTokensLayer, "No Tokens");
  text_layer_set_font(noTokensLayer, fonts_get_system_font(FONT_KEY_BITHAM_30_BLACK));
  text_layer_set_text_color(noTokensLayer, GColorBlack);
  text_layer_set_text_alignment(noTokensLayer, GTextAlignmentCenter);
  layer_add_child(rootLayer, (Layer*)noTokensLayer);

  codeListLayer = menu_layer_create(GRect(0,0,rootLayerRect.size.w, rootLayerRect.size.h - 4));

  MenuLayerCallbacks menuCallbacks = {
    .draw_row = draw_code_row,
    .get_num_rows = num_code_rows,
    .get_cell_height = get_cell_height
  };
  menu_layer_set_callbacks(codeListLayer, NULL, menuCallbacks);

  menu_layer_set_click_config_onto_window(codeListLayer, window);

  layer_add_child(rootLayer, (Layer*)codeListLayer);

  refresh_all();
}

void handle_tick(struct tm* tick_time, TimeUnits units_changed) {
  refresh_all();
  layer_mark_dirty(barLayer);
}

void handle_deinit() {
  if (persistentStoreNeedsWriteback) {
    // Write back persistent things
    persist_write_int(P_UTCOFFSET, utcOffset);
    persist_write_int(P_KEYCOUNT, key_list_length());

    KeyListNode* node = keyList;
    short idx = 0;
    while (node) {
      persist_write_data(P_KEYSTART + idx, node->key, sizeof(KeyInfo));
      idx++;
      node = node->next;
    }

    APP_LOG(APP_LOG_LEVEL_INFO, "Wrote %d keys", idx);
  }

  key_list_clear();
  menu_layer_destroy(codeListLayer);
  layer_destroy(barLayer);
  text_layer_destroy(noTokensLayer);
  window_destroy(window);
}

int main() {
  handle_init();
  tick_timer_service_subscribe(SECOND_UNIT, handle_tick);
  app_event_loop();
  handle_deinit();
}
