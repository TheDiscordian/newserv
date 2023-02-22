#include "ServerState.hh"

#include <string.h>

#include <memory>
#include <phosg/Network.hh>

#include "FileContentsCache.hh"
#include "IPStackSimulator.hh"
#include "Loggers.hh"
#include "NetworkAddresses.hh"
#include "SendCommands.hh"
#include "Text.hh"

using namespace std;



ServerState::ServerState()
  : dns_server_port(0),
    ip_stack_debug(false),
    allow_unregistered_users(false),
    allow_saving(true),
    item_tracking_enabled(true),
    episode_3_send_function_call_enabled(false),
    catch_handler_exceptions(true),
    ep3_behavior_flags(0),
    run_shell_behavior(RunShellBehavior::DEFAULT),
    ep3_card_auction_points(0),
    ep3_card_auction_min_size(0),
    ep3_card_auction_max_size(0),
    next_lobby_id(1),
    pre_lobby_event(0),
    ep3_menu_song(-1),
    local_address(0),
    external_address(0),
    proxy_allow_save_files(true),
    proxy_enable_login_options(false) {
  vector<shared_ptr<Lobby>> non_v1_only_lobbies;
  vector<shared_ptr<Lobby>> ep3_only_lobbies;

  for (size_t x = 0; x < 20; x++) {
    auto lobby_name = decode_sjis(string_printf("LOBBY%zu", x + 1));
    bool is_non_v1_only = (x > 9);
    bool is_ep3_only = (x > 14);

    shared_ptr<Lobby> l = this->create_lobby();
    l->flags |=
        Lobby::Flag::PUBLIC |
        Lobby::Flag::DEFAULT |
        Lobby::Flag::PERSISTENT |
        (is_non_v1_only ? Lobby::Flag::NON_V1_ONLY : 0) | 
        (is_ep3_only ? Lobby::Flag::EPISODE_3_ONLY : 0);
    l->block = 1;
    l->type = x; // type is the lobby (IE: the second number of 02-11)
    l->name = lobby_name;
    l->max_clients = 12;

    if (!is_non_v1_only) {
      this->public_lobby_search_order_v1.emplace_back(l);
    }
    if (!is_ep3_only) {
      this->public_lobby_search_order_non_v1.emplace_back(l);
    } else {
      ep3_only_lobbies.emplace_back(l);
    }
  }

  // Annoyingly, the CARD lobbies should be searched first, but are sent at the
  // end of the lobby list command, so we have to change t he search order
  // manually here.
  this->public_lobby_search_order_ep3 = this->public_lobby_search_order_non_v1;
  this->public_lobby_search_order_ep3.insert(
      this->public_lobby_search_order_ep3.begin(),
      ep3_only_lobbies.begin(),
      ep3_only_lobbies.end());
}

void ServerState::add_client_to_available_lobby(shared_ptr<Client> c) {
  shared_ptr<Lobby> added_to_lobby;

  if (c->preferred_lobby_id >= 0) {
    try {
      auto l = this->find_lobby(c->preferred_lobby_id);
      if (!l->is_game() && (l->flags & Lobby::Flag::PUBLIC)) {
        l->add_client(c);
        added_to_lobby = l;
      }
    } catch (const out_of_range&) { }
  }

  if (!added_to_lobby.get()) {
    const auto* search_order = &this->public_lobby_search_order_non_v1;
    if (c->flags & Client::Flag::IS_DC_V1) {
      search_order = &this->public_lobby_search_order_v1;
    } else if (c->flags & Client::Flag::IS_EPISODE_3) {
      search_order = &this->public_lobby_search_order_ep3;
    }
    for (const auto& l : *search_order) {
      try {
        l->add_client(c);
        added_to_lobby = l;
        break;
      } catch (const out_of_range&) { }
    }
  }

  if (!added_to_lobby) {
    // TODO: Add the user to a dynamically-created private lobby instead
    throw out_of_range("all lobbies full");
  }

  // Send a join message to the joining player, and notifications to all others
  this->send_lobby_join_notifications(added_to_lobby, c);
}

void ServerState::remove_client_from_lobby(shared_ptr<Client> c) {
  auto l = this->id_to_lobby.at(c->lobby_id);
  l->remove_client(c);
  if (!(l->flags & Lobby::Flag::PERSISTENT) && (l->count_clients() == 0)) {
    this->remove_lobby(l->lobby_id);
  } else {
    send_player_leave_notification(l, c->lobby_client_id);
  }
}

bool ServerState::change_client_lobby(
    shared_ptr<Client> c,
    shared_ptr<Lobby> new_lobby,
    bool send_join_notification,
    ssize_t required_client_id) {
  uint8_t old_lobby_client_id = c->lobby_client_id;

  shared_ptr<Lobby> current_lobby = this->find_lobby(c->lobby_id);
  try {
    if (current_lobby) {
      current_lobby->move_client_to_lobby(new_lobby, c, required_client_id);
    } else {
      new_lobby->add_client(c, required_client_id);
    }
  } catch (const out_of_range&) {
    return false;
  }

  if (current_lobby) {
    if (!(current_lobby->flags & Lobby::Flag::PERSISTENT) && (current_lobby->count_clients() == 0)) {
      this->remove_lobby(current_lobby->lobby_id);
    } else {
      send_player_leave_notification(current_lobby, old_lobby_client_id);
    }
  }
  if (send_join_notification) {
    this->send_lobby_join_notifications(new_lobby, c);
  }
  return true;
}

void ServerState::send_lobby_join_notifications(shared_ptr<Lobby> l,
    shared_ptr<Client> joining_client) {
  for (auto& other_client : l->clients) {
    if (!other_client) {
      continue;
    } else if (other_client == joining_client) {
      send_join_lobby(joining_client, l);
    } else {
      send_player_join_notification(other_client, l, joining_client);
    }
  }
  for (auto& watcher_l : l->watcher_lobbies) {
    for (auto& watcher_c : watcher_l->clients) {
      if (!watcher_c) {
        continue;
      }
      send_player_join_notification(watcher_c, watcher_l, joining_client);
    }
  }
}

shared_ptr<Lobby> ServerState::find_lobby(uint32_t lobby_id) {
  return this->id_to_lobby.at(lobby_id);
}

vector<shared_ptr<Lobby>> ServerState::all_lobbies() {
  vector<shared_ptr<Lobby>> ret;
  for (auto& it : this->id_to_lobby) {
    ret.emplace_back(it.second);
  }
  return ret;
}

shared_ptr<Lobby> ServerState::create_lobby() {
  while (this->id_to_lobby.count(this->next_lobby_id)) {
    this->next_lobby_id++;
  }
  shared_ptr<Lobby> l(new Lobby(this->next_lobby_id++));
  this->id_to_lobby.emplace(l->lobby_id, l);
  l->log.info("Created lobby");
  return l;
}

void ServerState::remove_lobby(uint32_t lobby_id) {
  auto lobby_it = this->id_to_lobby.find(lobby_id);
  if (lobby_it == this->id_to_lobby.end()) {
    throw logic_error("attempted to remove nonexistent lobby");
  }

  auto l = lobby_it->second;
  if (l->count_clients() != 0) {
    throw logic_error("attempted to delete lobby with clients in it");
  }

  if (l->flags & Lobby::Flag::IS_SPECTATOR_TEAM) {
    auto primary_l = l->watched_lobby.lock();
    if (primary_l) {
      primary_l->log.info("Unlinking watcher lobby %" PRIX32, l->lobby_id);
      primary_l->watcher_lobbies.erase(l);
    } else {
      l->log.info("Watched lobby is missing");
    }
    l->watched_lobby.reset();
  } else {
    // Tell all players in all spectator teams to go back to the lobby
    for (auto watcher_l : l->watcher_lobbies) {
      if (!(watcher_l->flags & Lobby::Flag::EPISODE_3_ONLY)) {
        throw logic_error("spectator team is not an Episode 3 lobby");
      }
      l->log.info("Disbanding watcher lobby %" PRIX32, watcher_l->lobby_id);
      send_command(watcher_l, 0xED, 0x00);
    }
  }

  l->log.info("Deleted lobby");
  this->id_to_lobby.erase(lobby_it);
}

shared_ptr<Client> ServerState::find_client(const std::u16string* identifier,
    uint64_t serial_number, shared_ptr<Lobby> l) {

  if ((serial_number == 0) && identifier) {
    try {
      serial_number = stoull(encode_sjis(*identifier), nullptr, 0);
    } catch (const exception&) { }
  }

  // look in the current lobby first
  if (l) {
    try {
      return l->find_client(identifier, serial_number);
    } catch (const out_of_range&) { }
  }

  // look in all lobbies if not found
  for (auto& other_l : this->all_lobbies()) {
    if (l == other_l) {
      continue; // don't bother looking again
    }
    try {
      return other_l->find_client(identifier, serial_number);
    } catch (const out_of_range&) { }
  }

  throw out_of_range("client not found");
}

uint32_t ServerState::connect_address_for_client(std::shared_ptr<Client> c) {
  if (c->channel.is_virtual_connection) {
    if (c->channel.remote_addr.ss_family != AF_INET) {
      throw logic_error("virtual connection is missing remote IPv4 address");
    }
    const auto* sin = reinterpret_cast<const sockaddr_in*>(&c->channel.remote_addr);
    return IPStackSimulator::connect_address_for_remote_address(
        ntohl(sin->sin_addr.s_addr));
  } else {
    // TODO: we can do something smarter here, like use the sockname to find
    // out which interface the client is connected to, and return that address
    if (is_local_address(c->channel.remote_addr)) {
      return this->local_address;
    } else {
      return this->external_address;
    }
  }
}



shared_ptr<const vector<MenuItem>> ServerState::information_menu_for_version(GameVersion version) {
  if ((version == GameVersion::DC) || (version == GameVersion::PC)) {
    return this->information_menu_v2;
  } else if ((version == GameVersion::GC) || (version == GameVersion::XB)) {
    return this->information_menu_v3;
  }
  throw out_of_range("no information menu exists for this version");
}

const vector<MenuItem>& ServerState::proxy_destinations_menu_for_version(GameVersion version) {
  switch (version) {
    case GameVersion::DC:
      return this->proxy_destinations_menu_dc;
    case GameVersion::PC:
      return this->proxy_destinations_menu_pc;
    case GameVersion::GC:
      return this->proxy_destinations_menu_gc;
    case GameVersion::XB:
      return this->proxy_destinations_menu_xb;
    default:
      throw out_of_range("no proxy destinations menu exists for this version");
  }
}

const vector<pair<string, uint16_t>>& ServerState::proxy_destinations_for_version(GameVersion version) {
  switch (version) {
    case GameVersion::DC:
      return this->proxy_destinations_dc;
    case GameVersion::PC:
      return this->proxy_destinations_pc;
    case GameVersion::GC:
      return this->proxy_destinations_gc;
    case GameVersion::XB:
      return this->proxy_destinations_xb;
    default:
      throw out_of_range("no proxy destinations menu exists for this version");
  }
}



void ServerState::set_port_configuration(
    const vector<PortConfiguration>& port_configs) {
  this->name_to_port_config.clear();
  this->number_to_port_config.clear();

  bool any_port_is_pc_console_detect = false;
  for (const auto& pc : port_configs) {
    shared_ptr<PortConfiguration> spc(new PortConfiguration(pc));
    if (!this->name_to_port_config.emplace(spc->name, spc).second) {
      // Note: This is a logic_error instead of a runtime_error because
      // port_configs comes from a JSON map, so the names should already all be
      // unique. In contrast, the user can define port configurations with the
      // same number while still writing valid JSON, so only one of these cases
      // can reasonably occur as a result of user behavior.
      throw logic_error("duplicate name in port configuration");
    }
    if (!this->number_to_port_config.emplace(spc->port, spc).second) {
      throw runtime_error("duplicate number in port configuration");
    }
    if (spc->behavior == ServerBehavior::PC_CONSOLE_DETECT) {
      any_port_is_pc_console_detect = true;
    }
  }

  if (any_port_is_pc_console_detect) {
    if (!this->name_to_port_config.count("pc-login")) {
      throw runtime_error("pc-login port is not defined, but some ports use the pc_console_detect behavior");
    }
    if (!this->name_to_port_config.count("console-login")) {
      throw runtime_error("console-login port is not defined, but some ports use the pc_console_detect behavior");
    }
  }
}



void ServerState::create_menus(shared_ptr<const JSONObject> config_json) {
  const auto& d = config_json->as_dict();

  shared_ptr<vector<MenuItem>> information_menu_v2(new vector<MenuItem>());
  shared_ptr<vector<MenuItem>> information_menu_v3(new vector<MenuItem>());
  shared_ptr<vector<u16string>> information_contents(new vector<u16string>());

  information_menu_v3->emplace_back(InformationMenuItemID::GO_BACK, u"Go back",
      u"Return to the\nmain menu", 0);
  {
    uint32_t item_id = 0;
    for (const auto& item : d.at("InformationMenuContents")->as_list()) {
      auto& v = item->as_list();
      information_menu_v2->emplace_back(item_id, decode_sjis(v.at(0)->as_string()),
          decode_sjis(v.at(1)->as_string()), 0);
      information_menu_v3->emplace_back(item_id, decode_sjis(v.at(0)->as_string()),
          decode_sjis(v.at(1)->as_string()), MenuItem::Flag::REQUIRES_MESSAGE_BOXES);
      information_contents->emplace_back(decode_sjis(v.at(2)->as_string()));
      item_id++;
    }
  }
  this->information_menu_v2 = information_menu_v2;
  this->information_menu_v3 = information_menu_v3;
  this->information_contents = information_contents;

  auto generate_proxy_destinations_menu = [&](
      vector<MenuItem>& ret_menu,
      vector<pair<string, uint16_t>>& ret_pds,
      const char* key) {
    try {
      map<string, shared_ptr<JSONObject>> sorted_jsons;
      for (const auto& it : d.at(key)->as_dict()) {
        sorted_jsons.emplace(it.first, it.second);
      }

      ret_menu.clear();
      ret_pds.clear();

      ret_menu.emplace_back(ProxyDestinationsMenuItemID::GO_BACK, u"Go back",
          u"Return to the\nmain menu", 0);
      ret_menu.emplace_back(ProxyDestinationsMenuItemID::OPTIONS, u"Options",
          u"Set proxy options", 0);

      uint32_t item_id = 0;
      for (const auto& item : sorted_jsons) {
        const string& netloc_str = item.second->as_string();
        const string& description = "$C7Remote server:\n$C6" + netloc_str;
        ret_menu.emplace_back(item_id, decode_sjis(item.first),
            decode_sjis(description), 0);
        ret_pds.emplace_back(parse_netloc(netloc_str));
        item_id++;
      }
    } catch (const out_of_range&) { }
  };

  generate_proxy_destinations_menu(
      this->proxy_destinations_menu_dc,
      this->proxy_destinations_dc,
      "ProxyDestinations-DC");
  generate_proxy_destinations_menu(
      this->proxy_destinations_menu_pc,
      this->proxy_destinations_pc,
      "ProxyDestinations-PC");
  generate_proxy_destinations_menu(
      this->proxy_destinations_menu_gc,
      this->proxy_destinations_gc,
      "ProxyDestinations-GC");
  generate_proxy_destinations_menu(
      this->proxy_destinations_menu_xb,
      this->proxy_destinations_xb,
      "ProxyDestinations-XB");

  try {
    const string& netloc_str = d.at("ProxyDestination-Patch")->as_string();
    this->proxy_destination_patch = parse_netloc(netloc_str);
    config_log.info("Patch server proxy is enabled with destination %s", netloc_str.c_str());
    for (auto& it : this->name_to_port_config) {
      if (it.second->version == GameVersion::PATCH) {
        it.second->behavior = ServerBehavior::PROXY_SERVER;
      }
    }
  } catch (const out_of_range&) {
    this->proxy_destination_patch.first = "";
    this->proxy_destination_patch.second = 0;
  }
  try {
    const string& netloc_str = d.at("ProxyDestination-BB")->as_string();
    this->proxy_destination_bb = parse_netloc(netloc_str);
    config_log.info("BB proxy is enabled with destination %s", netloc_str.c_str());
    for (auto& it : this->name_to_port_config) {
      if (it.second->version == GameVersion::BB) {
        it.second->behavior = ServerBehavior::PROXY_SERVER;
      }
    }
  } catch (const out_of_range&) {
    this->proxy_destination_bb.first = "";
    this->proxy_destination_bb.second = 0;
  }

  this->main_menu.emplace_back(MainMenuItemID::GO_TO_LOBBY, u"Go to lobby",
      u"Join the lobby", 0);
  this->main_menu.emplace_back(MainMenuItemID::INFORMATION, u"Information",
      u"View server\ninformation", MenuItem::Flag::INVISIBLE_ON_DCNTE | MenuItem::Flag::REQUIRES_MESSAGE_BOXES);
  uint32_t proxy_destinations_menu_item_flags =
      // DCNTE doesn't support multiple ship select menus without changing
      // servers (via a 19 command) apparently :(
      MenuItem::Flag::INVISIBLE_ON_DCNTE |
      (this->proxy_destinations_dc.empty() ? MenuItem::Flag::INVISIBLE_ON_DC : 0) |
      (this->proxy_destinations_pc.empty() ? MenuItem::Flag::INVISIBLE_ON_PC : 0) |
      (this->proxy_destinations_gc.empty() ? MenuItem::Flag::INVISIBLE_ON_GC : 0) |
      (this->proxy_destinations_xb.empty() ? MenuItem::Flag::INVISIBLE_ON_XB : 0) |
      MenuItem::Flag::INVISIBLE_ON_BB;
  this->main_menu.emplace_back(MainMenuItemID::PROXY_DESTINATIONS, u"Proxy server",
      u"Connect to another\nserver", proxy_destinations_menu_item_flags);
  this->main_menu.emplace_back(MainMenuItemID::DOWNLOAD_QUESTS, u"Download quests",
      u"Download quests", MenuItem::Flag::INVISIBLE_ON_DCNTE | MenuItem::Flag::INVISIBLE_ON_BB);
  if (!this->function_code_index->patch_menu_empty()) {
    this->main_menu.emplace_back(MainMenuItemID::PATCHES, u"Patches",
        u"Change game\nbehaviors", MenuItem::Flag::GC_ONLY | MenuItem::Flag::REQUIRES_SEND_FUNCTION_CALL);
  }
  if (!this->dol_file_index->empty()) {
    this->main_menu.emplace_back(MainMenuItemID::PROGRAMS, u"Programs",
        u"Run GameCube\nprograms", MenuItem::Flag::GC_ONLY | MenuItem::Flag::REQUIRES_SEND_FUNCTION_CALL | MenuItem::Flag::REQUIRES_SAVE_DISABLED);
  }
  this->main_menu.emplace_back(MainMenuItemID::DISCONNECT, u"Disconnect",
      u"Disconnect", 0);
  this->main_menu.emplace_back(MainMenuItemID::CLEAR_LICENSE, u"Clear license",
      u"Disconnect with an\ninvalid license error\nso you can enter a\ndifferent serial\nnumber, access key,\nor password",
      MenuItem::Flag::INVISIBLE_ON_DCNTE | MenuItem::Flag::INVISIBLE_ON_BB);

  try {
    this->welcome_message = decode_sjis(d.at("WelcomeMessage")->as_string());
  } catch (const out_of_range&) { }
  try {
    this->pc_patch_server_message = decode_sjis(d.at("PCPatchServerMessage")->as_string());
  } catch (const out_of_range&) { }
  try {
    this->bb_patch_server_message = decode_sjis(d.at("BBPatchServerMessage")->as_string());
  } catch (const out_of_range&) { }
}



shared_ptr<const string> ServerState::load_bb_file(
    const std::string& patch_index_filename,
    const std::string& gsl_filename,
    const std::string& bb_directory_filename) const {

  if (this->bb_patch_file_index) {
    // First, look in the patch tree's data directory
    string patch_index_path = "./data/" + patch_index_filename;
    try {
      auto ret = this->bb_patch_file_index->get(patch_index_path)->load_data();
      static_game_data_log.info("Loaded %s from file in BB patch tree", patch_index_path.c_str());
      return ret;
    } catch (const out_of_range&) {
      static_game_data_log.info("%s missing from BB patch tree", patch_index_path.c_str());
    }
  }

  if (this->bb_data_gsl) {
    // Second, look in the patch tree's data.gsl file
    const string& effective_gsl_filename = gsl_filename.empty() ? patch_index_filename : gsl_filename;
    try {
      // TODO: It's kinda not great that we copy the data here; find a way to
      // avoid doing this (also in the below case)
      shared_ptr<string> ret(new string(this->bb_data_gsl->get_copy(effective_gsl_filename)));
      static_game_data_log.info("Loaded %s from data.gsl in BB patch tree", effective_gsl_filename.c_str());
      return ret;
    } catch (const out_of_range&) {
      static_game_data_log.info("%s missing from data.gsl in BB patch tree", effective_gsl_filename.c_str());
    }

    // Third, look in data.gsl without the filename extension
    size_t dot_offset = effective_gsl_filename.rfind('.');
    if (dot_offset != string::npos) {
      string no_ext_gsl_filename = effective_gsl_filename.substr(0, dot_offset);
      try {
        shared_ptr<string> ret(new string(this->bb_data_gsl->get_copy(no_ext_gsl_filename)));
        static_game_data_log.info("Loaded %s from data.gsl in BB patch tree", no_ext_gsl_filename.c_str());
        return ret;
      } catch (const out_of_range&) {
        static_game_data_log.info("%s missing from data.gsl in BB patch tree", no_ext_gsl_filename.c_str());
      }
    }
  }

  // Finally, look in system/blueburst
  const string& effective_bb_directory_filename = bb_directory_filename.empty() ? patch_index_filename : bb_directory_filename;
  static FileContentsCache cache(10 * 60 * 1000 * 1000); // 10 minutes
  try {
    auto ret = cache.get_or_load("system/blueburst/" + effective_bb_directory_filename);
    static_game_data_log.info("Loaded %s", effective_bb_directory_filename.c_str());
    return ret.file->data;
  } catch (const exception& e) {
    static_game_data_log.info("%s missing from system/blueburst", effective_bb_directory_filename.c_str());
    static_game_data_log.error("%s not found in any source", patch_index_filename.c_str());
    throw cannot_open_file(patch_index_filename);
  }
}
