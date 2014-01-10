#include <pebble.h>

// the window
static Window *window;

// Constants for damage types, resistances, and stats
#define NO_RESISTANCES 0
#define SWORD_DAMAGE 1
#define MAGIC_DAMAGE 2
#define BOW_DAMAGE 4
#define HEALTH_STAT 8

// Constants for game states
#define BATTLE 0
#define TRAVEL 1
#define WELCOME 2
#define DEATH 3

// Persistent storage keys
#define PLAYER_MAX_HEALTH_KEY 0
#define PLAYER_CURRENT_HEALTH_KEY 1
#define PLAYER_SWORD_DAMAGE_KEY 2
#define PLAYER_MAGIC_DAMAGE_KEY 3
#define PLAYER_BOW_DAMAGE_KEY 4
#define TRAVEL_PROGRESS_KEY 5
#define MONSTER_INDEX_KEY 6
#define MONSTER_CURRENT_HEALTH_KEY 7
#define STATE_KEY 8

// Text Fields
static TextLayer *enemy_name;
static TextLayer *player_health_label;
static TextLayer *player_health;
static TextLayer *death_text;
static TextLayer *welcome_text;
static TextLayer *press_a_key;

// Images/Sprites
static BitmapLayer *sword_layer;
static GBitmap *sword_icon;
static BitmapLayer *magic_layer;
static GBitmap *magic_icon;
static BitmapLayer *bow_layer;
static GBitmap *bow_icon;
static BitmapLayer *monster_layer;
static GBitmap *monster_sprite;
static Layer *monster_health_layer;

// struct to hold monsters
typedef struct {
    int health;
    int resistances;
    float hit_chance;
    int damage;
    uint32_t sprite;
    int stat_boost;
    const char* name;
} Monster;

static Monster tentacle_mage;
static Monster ent;
static Monster horned_guard;
static Monster* monster_index[3];

// Player stats
static int player_max_health;
static int player_sword_damage;
static int player_magic_damage;
static int player_bow_damage;


// this holds a pointer to the monster currently being fought
static Monster *current_battle;
// This value holds the current health of the monster we're fighting
static int current_monster_health = 0;
// This holds the current player health
// TODO this will change when stats are implemented
static int current_player_health;
static char player_health_str[3];
// This int represents what sort of mode the game is in right now
// This will be overwritten at launch
static int state = WELCOME;

static AppTimer *travel_timer;
static AccelData accel_prev;
static AccelData accel_current;
// TODO pull this out of persistent storage
static int movement_total = 0;

void state_transition(int new_state);

float rng() {
    return (float) rand() / (float) RAND_MAX;
}

int custom_abs(int value) {
    uint32_t temp = value >> 31;     // make a mask of the sign bit
    value ^= temp;                   // toggle the bits if value is negative
    value += temp & 1;               // add one if value was negative
    return value;
}

Monster* random_encounter() {
    int index = rand() % 3;
    persist_write_int(MONSTER_INDEX_KEY, index);
    persist_write_int(MONSTER_CURRENT_HEALTH_KEY, monster_index[index]->health);
    return monster_index[index];
    /*
    float roll = rng();
    if (roll < 0.33) {
        return &ent;
    } else if (roll >= 0.33 && roll < 0.66) {
        return &tentacle_mage;
    } else {
        return &horned_guard;
    }
    */
}

void increase_stats(int attribute) {
    if (attribute == HEALTH_STAT) {
        player_max_health += 1;
        current_player_health += 1;
        persist_write_int(PLAYER_MAX_HEALTH_KEY, player_max_health);
        persist_write_int(PLAYER_CURRENT_HEALTH_KEY, current_player_health);
    } else if (attribute == SWORD_DAMAGE) {
        player_sword_damage += 1;
        persist_write_int(PLAYER_SWORD_DAMAGE_KEY, player_sword_damage); 
    } else if (attribute == MAGIC_DAMAGE) {
        player_magic_damage += 1;
        persist_write_int(PLAYER_MAGIC_DAMAGE_KEY, player_magic_damage); 
    } else if (attribute == BOW_DAMAGE) {
        player_bow_damage += 1;
        persist_write_int(PLAYER_BOW_DAMAGE_KEY, player_bow_damage); 
    }
}

void clear_stats() {
    persist_delete(PLAYER_MAX_HEALTH_KEY);
    persist_delete(PLAYER_CURRENT_HEALTH_KEY);
    persist_delete(PLAYER_SWORD_DAMAGE_KEY);
    persist_delete(PLAYER_MAGIC_DAMAGE_KEY);
    persist_delete(PLAYER_BOW_DAMAGE_KEY);
    persist_delete(MONSTER_INDEX_KEY);
    persist_delete(MONSTER_CURRENT_HEALTH_KEY);
    persist_delete(STATE_KEY);
}

void attack(int type) {
    int damage = 0;
    if (type == SWORD_DAMAGE) {
        damage = player_sword_damage;
    } else if (type == MAGIC_DAMAGE) {
        damage = player_magic_damage;
    } else if (type == BOW_DAMAGE) {
        damage = player_bow_damage;
    }
    if ((current_battle->resistances & type) == 0) {
        current_monster_health -= damage;
    } else {
        current_monster_health -= (int)((float) damage / 5.0);
    }
    if (current_monster_health <= 0) {
        increase_stats(current_battle->stat_boost);
        persist_delete(MONSTER_CURRENT_HEALTH_KEY);
        persist_delete(MONSTER_INDEX_KEY);
        state_transition(TRAVEL);
    } else {
        persist_write_int(MONSTER_CURRENT_HEALTH_KEY, current_monster_health);
        if (rng() < current_battle->hit_chance) {
            current_player_health -= current_battle->damage;
            if (current_player_health <= 0) {
                clear_stats();
                state_transition(DEATH);
                return;
            }
            persist_write_int(PLAYER_CURRENT_HEALTH_KEY, current_player_health);
            snprintf(player_health_str, 3, "%d", current_player_health);
            text_layer_set_text(player_health, player_health_str);
        }
    }
    layer_mark_dirty(monster_health_layer);
}

static void select_click_handler(ClickRecognizerRef recognizer, void *context) {
    if (state == BATTLE) {
        attack(MAGIC_DAMAGE);
    } else if (state == DEATH) {
        state_transition(WELCOME);
    } else if (state == WELCOME) {
        state_transition(TRAVEL);
    }

}

static void up_click_handler(ClickRecognizerRef recognizer, void *context) {
    if (state == BATTLE) {
        attack(SWORD_DAMAGE);
    } else if (state == DEATH) {
        state_transition(WELCOME);
    } else if (state == WELCOME) {
        state_transition(TRAVEL);
    }


}

static void down_click_handler(ClickRecognizerRef recognizer, void *context) {
    if (state == BATTLE) {
        attack(BOW_DAMAGE);
    } else if (state == DEATH) {
        state_transition(WELCOME);
    } else if (state == WELCOME) {
        state_transition(TRAVEL);
    }


}

static void click_config_provider(void *context) {
  window_single_click_subscribe(BUTTON_ID_SELECT, select_click_handler);
  window_single_click_subscribe(BUTTON_ID_UP, up_click_handler);
  window_single_click_subscribe(BUTTON_ID_DOWN, down_click_handler);
}

static void handle_accel(AccelData *accel_data, uint32_t num_samples) { 
    // do nothing                                                       
}                                                                       

static void query_accel(void *context) {
    accel_prev = accel_current;
    accel_service_peek(&accel_current);
    int sum = 0;
    sum += accel_current.x - accel_prev.x;
    sum += accel_current.y - accel_prev.y;
    sum += accel_current.z - accel_prev.z;
    movement_total += custom_abs(sum);
    APP_LOG(APP_LOG_LEVEL_DEBUG, "Adding %d to total, total is now %d.", custom_abs(sum), movement_total);
    if (movement_total > 10000) {
        movement_total = 0;
        state_transition(BATTLE);
    }

    travel_timer = app_timer_register(3000, query_accel, NULL);
}

void stats_load() {
    if (persist_exists(PLAYER_MAX_HEALTH_KEY)) {
        player_max_health = persist_read_int(PLAYER_MAX_HEALTH_KEY);
        current_player_health = persist_read_int(PLAYER_CURRENT_HEALTH_KEY);
        player_sword_damage = persist_read_int(PLAYER_SWORD_DAMAGE_KEY);
        player_magic_damage = persist_read_int(PLAYER_MAGIC_DAMAGE_KEY);
        player_bow_damage = persist_read_int(PLAYER_BOW_DAMAGE_KEY);
    } else {
        APP_LOG(APP_LOG_LEVEL_DEBUG, "Stats did not exist, resetting.");
        player_max_health = 10;
        current_player_health = player_max_health;
        player_sword_damage = 1;
        player_magic_damage = 1;
        player_bow_damage = 1;
        persist_write_int(PLAYER_MAX_HEALTH_KEY, player_max_health);
        persist_write_int(PLAYER_CURRENT_HEALTH_KEY, player_max_health);
        persist_write_int(PLAYER_SWORD_DAMAGE_KEY, player_sword_damage);
        persist_write_int(PLAYER_MAGIC_DAMAGE_KEY, player_magic_damage);
        persist_write_int(PLAYER_BOW_DAMAGE_KEY, player_bow_damage);
    }
}

static void monster_load() { 
    // Tentacle Mage
    tentacle_mage.health = 5;
    tentacle_mage.resistances = MAGIC_DAMAGE;
    tentacle_mage.hit_chance = 0.3;
    tentacle_mage.damage = 2;
    tentacle_mage.sprite = RESOURCE_ID_MONSTER_TENTACLE_MAGE;
    tentacle_mage.name = "Tentacle Mage";
    tentacle_mage.stat_boost = MAGIC_DAMAGE;
    
    // Ent
    ent.health = 10;
    ent.resistances = BOW_DAMAGE;
    ent.hit_chance = 0.5;
    ent.damage = 1;
    ent.sprite = RESOURCE_ID_MONSTER_ENT;
    ent.name = "Ent";
    ent.stat_boost = MAGIC_DAMAGE;

    // Horned Guard
    horned_guard.health = 7;
    horned_guard.resistances = SWORD_DAMAGE;
    horned_guard.hit_chance = 0.5;
    horned_guard.damage = 1;
    horned_guard.sprite = RESOURCE_ID_MONSTER_HORNED_GUARD;
    horned_guard.name = "Horned Guard";
    horned_guard.stat_boost = SWORD_DAMAGE;

    monster_index[0] = &tentacle_mage;
    monster_index[1] = &ent;
    monster_index[2] = &horned_guard;
}

void monster_health_update(Layer *layer, GContext* ctx) {
    graphics_context_set_fill_color(ctx, GColorBlack);
    float percent_health = ((float) current_monster_health) / ((float) current_battle->health);
    graphics_fill_rect(ctx, GRect(0, 0, (int)(percent_health * 75.0), 4), 0, GCornerNone);
}

static void battle_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  if (persist_exists(MONSTER_CURRENT_HEALTH_KEY) && persist_exists(MONSTER_INDEX_KEY)) {
      current_battle = monster_index[persist_read_int(MONSTER_INDEX_KEY)]; 
      current_monster_health = persist_read_int(MONSTER_CURRENT_HEALTH_KEY);
  } else {
      current_battle = random_encounter();
      current_monster_health = current_battle->health;
  }

  enemy_name = text_layer_create((GRect) { .origin = { 10, 10 }, .size = { 100, 20 } });
  text_layer_set_text(enemy_name, current_battle->name);
  text_layer_set_text_alignment(enemy_name, GTextAlignmentCenter);
  layer_add_child(window_layer, text_layer_get_layer(enemy_name));

  player_health_label = text_layer_create((GRect) { .origin = { 10, 130 }, .size = { 80, 20 } });
  text_layer_set_text(player_health_label, "Your Health:");
  layer_add_child(window_layer, text_layer_get_layer(player_health_label));

  player_health = text_layer_create((GRect) { .origin = { 90, 130 }, .size = { 40, 20 } });
  snprintf(player_health_str, 3, "%d", current_player_health);
  text_layer_set_text(player_health, player_health_str);
  layer_add_child(window_layer, text_layer_get_layer(player_health));

  sword_layer = bitmap_layer_create((GRect) { .origin = { 130, 10 }, .size = { 10, 10 } }); 
  sword_icon = gbitmap_create_with_resource(RESOURCE_ID_SWORD_ICON);
  bitmap_layer_set_bitmap(sword_layer, sword_icon);
  layer_add_child(window_layer, bitmap_layer_get_layer(sword_layer));

  magic_layer = bitmap_layer_create((GRect) { .origin = { 130, 65 }, .size = { 10, 10 } }); 
  magic_icon = gbitmap_create_with_resource(RESOURCE_ID_MAGIC_ICON);
  bitmap_layer_set_bitmap(magic_layer, magic_icon);
  layer_add_child(window_layer, bitmap_layer_get_layer(magic_layer));

  bow_layer = bitmap_layer_create((GRect) { .origin = { 130, 130 }, .size = { 10, 10 } }); 
  bow_icon = gbitmap_create_with_resource(RESOURCE_ID_BOW_ICON);
  bitmap_layer_set_bitmap(bow_layer, bow_icon);
  layer_add_child(window_layer, bitmap_layer_get_layer(bow_layer));

  monster_layer = bitmap_layer_create((GRect) { .origin = { 20, 40 }, .size = { 75, 75 } });
  monster_sprite = gbitmap_create_with_resource(current_battle->sprite);
  bitmap_layer_set_bitmap(monster_layer, monster_sprite);
  layer_add_child(window_layer, bitmap_layer_get_layer(monster_layer));

  GRect monster_health_frame = GRect(20, 125, 75, 4);
  monster_health_layer = layer_create(monster_health_frame);
  layer_set_update_proc(monster_health_layer, monster_health_update);
  layer_add_child(window_layer, monster_health_layer); 
}

static void battle_unload(Window *window) {
  text_layer_destroy(enemy_name);
  text_layer_destroy(player_health_label);
  text_layer_destroy(player_health);

  gbitmap_destroy(sword_icon);
  gbitmap_destroy(magic_icon);
  gbitmap_destroy(bow_icon);
  gbitmap_destroy(monster_sprite);

  bitmap_layer_destroy(sword_layer);
  bitmap_layer_destroy(magic_layer);
  bitmap_layer_destroy(bow_layer);
  bitmap_layer_destroy(monster_layer);

  layer_destroy(monster_health_layer);
}

static void death_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(window_layer);
  death_text = text_layer_create((GRect) { .origin = { 0, 30 }, .size = { bounds.size.w, 90 } });
  text_layer_set_text(death_text, "YOU\nDIED");
  text_layer_set_text_alignment(death_text, GTextAlignmentCenter);
  text_layer_set_font(death_text, fonts_load_custom_font(resource_get_handle(RESOURCE_ID_STONECROSS_20)));
  layer_add_child(window_layer, text_layer_get_layer(death_text)); 

  press_a_key = text_layer_create((GRect) { .origin = { 0, 130 }, .size = { bounds.size.w, 20 } });
  text_layer_set_text(press_a_key, "Press a button");
  text_layer_set_text_alignment(press_a_key, GTextAlignmentCenter);
  layer_add_child(window_layer, text_layer_get_layer(press_a_key)); 
}

static void death_unload(Window *window) {
  text_layer_destroy(death_text); 
  text_layer_destroy(press_a_key); 
}

static void welcome_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(window_layer);
  welcome_text = text_layer_create((GRect) { .origin = { 0, 30 }, .size = { bounds.size.w, 90 } });
  text_layer_set_text(welcome_text, "THE\nLEGEND\nOF XOR");
  text_layer_set_text_alignment(welcome_text, GTextAlignmentCenter);
  text_layer_set_font(welcome_text, fonts_load_custom_font(resource_get_handle(RESOURCE_ID_STONECROSS_20)));
  layer_add_child(window_layer, text_layer_get_layer(welcome_text)); 

  press_a_key = text_layer_create((GRect) { .origin = { 0, 130 }, .size = { bounds.size.w, 20 } });
  text_layer_set_text(press_a_key, "Press a button");
  text_layer_set_text_alignment(press_a_key, GTextAlignmentCenter);
  layer_add_child(window_layer, text_layer_get_layer(press_a_key)); 

  // this is so that when we restart, player variables are set back to the baseline
  stats_load();
}

static void welcome_unload(Window *window) {
  text_layer_destroy(welcome_text); 
  text_layer_destroy(press_a_key); 
}

static void travel_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(window_layer);
  welcome_text = text_layer_create((GRect) { .origin = { 0, 30 }, .size = { bounds.size.w, 90 } });
  text_layer_set_text(welcome_text, "TRAVELING");
  text_layer_set_text_alignment(welcome_text, GTextAlignmentCenter);
  text_layer_set_font(welcome_text, fonts_load_custom_font(resource_get_handle(RESOURCE_ID_STONECROSS_20)));
  layer_add_child(window_layer, text_layer_get_layer(welcome_text)); 

  accel_data_service_subscribe(0, handle_accel);
  accel_service_peek(&accel_prev);
  travel_timer = app_timer_register(3000, query_accel, NULL);
}

static void travel_unload(Window *window) {
  text_layer_destroy(welcome_text); 

  accel_data_service_unsubscribe();
  app_timer_cancel(travel_timer);
}

void state_transition(int new_state) {
    if (state == BATTLE) {
        battle_unload(window);
    } else if (state == DEATH) {
        death_unload(window);
    } else if (state == WELCOME) {
        welcome_unload(window);
    } else if (state == TRAVEL) {
        travel_unload(window);
    }
    state = new_state;
    persist_write_int(STATE_KEY, state);
    APP_LOG(APP_LOG_LEVEL_DEBUG, "Wrote a state to persistent storage.");
    if (state == BATTLE) {
        battle_load(window);
    } else if (state == DEATH) {
        death_load(window);
    } else if (state == WELCOME) {
        welcome_load(window);
    } else if (state == TRAVEL) {
        travel_load(window);
    }
}

static void window_load(Window *window) {
  if (persist_exists(STATE_KEY)) {
      state = persist_read_int(STATE_KEY);
  } else {
      state = WELCOME;
  }
  if (state == BATTLE) {
      battle_load(window);
  } else if (state == DEATH) {
      death_load(window);
  } else if (state == WELCOME) {
      welcome_load(window);
  } else if (state == TRAVEL) {
      travel_load(window);
  }
}

static void window_unload(Window *window) {
  if (state == BATTLE) {
      battle_unload(window);
  } else if (state == DEATH) {
      death_unload(window);
  } else if (state == WELCOME) {
      welcome_unload(window);
  } else if (state == TRAVEL) {
      travel_unload(window);
  }
}

static void init(void) {
  monster_load();
  stats_load();
  window = window_create();
  window_set_click_config_provider(window, click_config_provider);
  window_set_window_handlers(window, (WindowHandlers) {
    .load = window_load,
    .unload = window_unload,
  });
  const bool animated = true;
  window_stack_push(window, animated);
}

static void deinit(void) {
  window_destroy(window);
}

int main(void) {
  init();

  if (persist_exists(STATE_KEY)) {
      APP_LOG(APP_LOG_LEVEL_DEBUG, "Saved state is %d.", (int)persist_read_int(STATE_KEY));
  } else {
      APP_LOG(APP_LOG_LEVEL_DEBUG, "No saved state found.");
  }


  app_event_loop();
  deinit();
}
