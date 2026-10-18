#pragma once
#include <array>
#include <algorithm>
#include <string_view>
#include <vector>

enum class Item : int {
  None,
  Grass,
  Dirt,
  Stone,
  Sand,
  Log,
  Leaves,
  Water,
  Snow,
  Elytra,
  Rocket,
  Apple,
  Steak,
  Helmet,
  Chestplate,
  Leggings,
  Boots,
  Planks,
  Stick,
  CraftingTable
};
struct Stack {
  Item item = Item::None;
  int count = 0;
  int durability = 432;
  bool empty() const { return item == Item::None || count <= 0; }
  void clear() { *this = {}; }
};
inline int stackLimit(Item item) {
  return item == Item::Elytra || (item >= Item::Helmet && item <= Item::Boots) ? 1 : 64;
}
inline std::string_view itemName(Item i) {
  constexpr std::string_view names[] = {
      "Empty",           "Grass",      "Dirt",        "Stone",       "Sand",
      "Oak log",         "Leaves",     "Water",       "Snow",        "Elytra",
      "Firework rocket", "Apple",      "Cooked beef", "Iron helmet", "Iron chestplate",
      "Iron leggings",   "Iron boots", "Oak planks",  "Stick",       "Crafting table"};
  int n = int(i);
  return n >= 0 && n < 20 ? names[n] : "Unknown";
}
inline int itemTile(Item item) {
  switch (item) {
  case Item::Grass:
    return 0;
  case Item::Dirt:
    return 2;
  case Item::Stone:
    return 3;
  case Item::Sand:
    return 4;
  case Item::Log:
    return 5;
  case Item::Leaves:
    return 7;
  case Item::Water:
    return 8;
  case Item::Snow:
    return 9;
  case Item::Elytra:
    return 10;
  case Item::Rocket:
    return 11;
  case Item::Steak:
    return 12;
  case Item::Apple:
    return 13;
  case Item::Helmet:
    return 18;
  case Item::Chestplate:
    return 19;
  case Item::Leggings:
    return 20;
  case Item::Boots:
    return 21;
  case Item::Planks:
    return 22;
  case Item::Stick:
    return 23;
  case Item::CraftingTable:
    return 24;
  default:
    return -1;
  }
}
inline bool isFood(Item i) { return i == Item::Apple || i == Item::Steak; }
inline bool isBlock(Item i) {
  return (int(i) >= 1 && int(i) <= 8 && i != Item::Water) || i == Item::Planks ||
         i == Item::CraftingTable;
}
inline int placedBlock(Item item) {
  return item == Item::Planks ? 9 : item == Item::CraftingTable ? 10 : int(item);
}
inline Item blockItem(int block) {
  return block == 9 ? Item::Planks : block == 10 ? Item::CraftingTable : Item(block);
}
struct Inventory {
  std::array<Stack, 36> slots{};
  Stack chest{}, cursor{}, helmet{}, leggings{}, boots{}, offhand{};
  std::array<Stack, 4> crafting{};
  int selected = 0;
  static Inventory starter() {
    Inventory bag;
    bag.slots[0] = {Item::Rocket, 64};
    bag.slots[1] = {Item::Elytra, 1};
    return bag;
  }
  void flightOnly() {
    for (int i = 0; i <= 44; i++) {
      auto *s = slot(i);
      if (s && !s->empty() && s->item != Item::Rocket && s->item != Item::Elytra)
        s->clear();
    }
    if (!cursor.empty() && cursor.item != Item::Rocket && cursor.item != Item::Elytra)
      cursor.clear();
    if (find(Item::Rocket) < 0)
      add(Item::Rocket, 64);
    if (find(Item::Elytra) < 0 && chest.item != Item::Elytra && offhand.item != Item::Elytra &&
        cursor.item != Item::Elytra)
      add(Item::Elytra, 1);
  }
  Stack &held() { return slots[selected]; }
  const Stack &held() const { return slots[selected]; }
  int find(Item item) const {
    for (int i = 0; i < 36; i++)
      if (!slots[i].empty() && slots[i].item == item)
        return i;
    return -1;
  }
  int count(Item item) const {
    int n = 0;
    for (const auto &s : slots)
      if (s.item == item)
        n += s.count;
    return n;
  }
  bool consume(int index, int n = 1) {
    if (index < 0 || index >= 36 || slots[index].empty() || slots[index].count < n)
      return false;
    slots[index].count -= n;
    if (!slots[index].count)
      slots[index].clear();
    return true;
  }
  bool add(Item item, int n = 1, int durability = 432) {
    if (item == Item::None || n <= 0)
      return false;
    auto copy = slots;
    for (auto &s : copy)
      if (s.item == item && !s.empty() && s.count < stackLimit(item) && item != Item::Elytra) {
        int a = std::min(n, stackLimit(item) - s.count);
        s.count += a;
        n -= a;
      }
    for (auto &s : copy)
      if (s.empty() && n) {
        int a = std::min(n, stackLimit(item));
        s = {item, a, durability};
        n -= a;
      }
    if (n)
      return false;
    slots = copy;
    return true;
  }
  bool toggleEquipment() {
    if (!chest.empty()) {
      if (!add(chest.item, chest.count, chest.durability))
        return false;
      chest.clear();
      return true;
    }
    if (held().item != Item::Elytra || held().empty()) {
      if (cursor.item == Item::Elytra && !cursor.empty()) {
        chest = cursor;
        cursor.clear();
        return true;
      }
      return false;
    }
    chest = held();
    held().clear();
    return true;
  }
  Stack *slot(int index) {
    if (index >= 0 && index < 36)
      return &slots[index];
    if (index == 36)
      return &chest;
    if (index == 37)
      return &helmet;
    if (index == 38)
      return &leggings;
    if (index == 39)
      return &boots;
    if (index == 40)
      return &offhand;
    if (index >= 41 && index <= 44)
      return &crafting[index - 41];
    return nullptr;
  }
  const Stack *slot(int index) const { return const_cast<Inventory *>(this)->slot(index); }
  static bool accepts(int index, Item item) {
    if (item == Item::None)
      return true;
    if (index == 36)
      return item == Item::Elytra || item == Item::Chestplate;
    if (index == 37)
      return item == Item::Helmet;
    if (index == 38)
      return item == Item::Leggings;
    if (index == 39)
      return item == Item::Boots;
    return index >= 0 && index <= 44;
  }
  static bool same(const Stack &a, const Stack &b) {
    return a.item == b.item && (stackLimit(a.item) > 1 || a.durability == b.durability);
  }
  Stack output() const {
    int logs = 0, planks = 0, other = 0;
    for (const auto &s : crafting)
      if (!s.empty()) {
        if (s.item == Item::Log)
          ++logs;
        else if (s.item == Item::Planks)
          ++planks;
        else
          ++other;
      }
    if (other)
      return {};
    if (logs == 1 && planks == 0)
      return {Item::Planks, 4};
    if (logs == 0 && planks == 4)
      return {Item::CraftingTable, 1};
    if (logs == 0 && planks == 2 &&
        ((!crafting[0].empty() && !crafting[2].empty()) ||
         (!crafting[1].empty() && !crafting[3].empty())))
      return {Item::Stick, 4};
    return {};
  }
  bool craft(bool intoBag = false) {
    Stack result = output();
    if (result.empty())
      return false;
    if (intoBag) {
      if (!add(result.item, result.count, result.durability))
        return false;
    } else {
      if (!cursor.empty() &&
          (!same(cursor, result) || cursor.count + result.count > stackLimit(result.item)))
        return false;
      if (cursor.empty())
        cursor = result;
      else
        cursor.count += result.count;
    }
    for (auto &s : crafting)
      if (!s.empty()) {
        if (--s.count == 0)
          s.clear();
      }
    return true;
  }
  void click(int index, bool right = false) {
    if (index == 45) {
      craft();
      return;
    }
    auto *target = slot(index);
    if (!target)
      return;
    auto &destination = *target;
    if (cursor.empty()) {
      if (destination.empty())
        return;
      cursor = destination;
      if (right) {
        cursor.count = (destination.count + 1) / 2;
        destination.count -= cursor.count;
        if (!destination.count)
          destination.clear();
      } else
        destination.clear();
    } else if (accepts(index, cursor.item)) {
      if (destination.empty() || same(destination, cursor)) {
        if (destination.empty())
          destination = {cursor.item, 0, cursor.durability};
        int n =
            std::min(right ? 1 : cursor.count, stackLimit(destination.item) - destination.count);
        destination.count += n;
        cursor.count -= n;
        if (!cursor.count)
          cursor.clear();
      } else
        std::swap(destination, cursor);
    }
  }
  bool clickChest() {
    if (!cursor.empty() && !accepts(36, cursor.item))
      return false;
    click(36);
    return true;
  }
  void quickMove(int index) {
    if (index == 45) {
      while (craft(true)) {
      }
      return;
    }
    auto *target = slot(index);
    if (!target || target->empty())
      return;
    if (index < 36) {
      for (int armor = 36; armor <= 39; armor++)
        if (accepts(armor, target->item) && slot(armor)->empty()) {
          *slot(armor) = *target;
          target->clear();
          return;
        }
    } else {
      if (add(target->item, target->count, target->durability))
        target->clear();
      return;
    }
    int first = index < 9 ? 9 : 0, last = index < 9 ? 36 : 9;
    Stack moving = *target;
    for (int pass = 0; pass < 2; pass++)
      for (int i = first; i < last; i++) {
        auto &s = slots[i];
        if (pass == 0 ? (s.empty() || !same(s, moving)) : !s.empty())
          continue;
        if (s.empty())
          s = {moving.item, 0, moving.durability};
        int n = std::min(moving.count, stackLimit(s.item) - s.count);
        s.count += n;
        moving.count -= n;
        if (!moving.count) {
          target->clear();
          return;
        }
      }
    *target = moving;
  }
  bool swapHotbar(int index, int hotbar) {
    auto *target = slot(index);
    if (!target || hotbar < 0 || hotbar >= 9 || !accepts(index, slots[hotbar].item))
      return false;
    std::swap(*target, slots[hotbar]);
    return true;
  }
  bool swapOffhand(int index) {
    auto *target = slot(index);
    if (!target || !accepts(index, offhand.item))
      return false;
    std::swap(*target, offhand);
    return true;
  }
  void collectAll() {
    if (cursor.empty() || stackLimit(cursor.item) == 1)
      return;
    for (int pass = 0; pass < 2; pass++)
      for (int i = 0; i <= 44; i++) {
        auto *s = slot(i);
        if (!s || s->empty() || !same(*s, cursor) || (pass == 0 && s->count == stackLimit(s->item)))
          continue;
        int n = std::min(s->count, stackLimit(cursor.item) - cursor.count);
        cursor.count += n;
        s->count -= n;
        if (!s->count)
          s->clear();
      }
  }
  void distribute(const std::vector<int> &indices, bool right) {
    if (cursor.empty() || indices.empty())
      return;
    const int each = right ? 1 : cursor.count / int(indices.size());
    for (int index : indices) {
      auto *s = slot(index);
      if (!s || !accepts(index, cursor.item) || (!s->empty() && !same(*s, cursor)))
        continue;
      if (s->empty())
        *s = {cursor.item, 0, cursor.durability};
      const int n = std::min({each, cursor.count, stackLimit(s->item) - s->count});
      s->count += n;
      cursor.count -= n;
      if (!s->count)
        s->clear();
      if (!cursor.count) {
        cursor.clear();
        break;
      }
    }
  }
};
