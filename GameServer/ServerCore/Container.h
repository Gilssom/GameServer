#pragma once
#include "Types.h"
#include "Allocator.h"
#include <vector>
#include <list>
#include <queue>
#include <stack>
#include <map>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <functional>

using namespace std;

template<typename T>
using Vector = vector<T, StlAllocator<T>>;

template<typename T>
using List = list<T, StlAllocator<T>>;

template<typename Key, typename T, typename Pred = less<Key>>
using Map = map<Key, T, Pred, StlAllocator<pair<const Key, T>>>;

template<typename Key, typename Pred = less<Key>>
using Set = set<Key, Pred, StlAllocator<Key>>;

template<typename T>
using Deque = deque<T, StlAllocator<T>>;

template<typename T, typename Container = Deque<T>>
using Queue = queue<T, Container>;

template<typename T, typename Container = Deque<T>>
using Stack = stack<T, Container>;

template<typename T, typename Container = Vector<T>, typename Pred = less<typename Container::value_type>>
using PriorityQueue = priority_queue<T, Container, Pred>;

using String = basic_string<char, char_traits<char>, StlAllocator<char>>;
using WString = basic_string<wchar_t, char_traits<wchar_t>, StlAllocator<wchar_t>>;

template<typename Key, typename T, typename Hasher = hash<Key>, typename KeyEq = equal_to<Key>>
using HashMap = unordered_map<Key, T, Hasher, KeyEq, StlAllocator<pair<const Key, T>>>;

template<typename Key, typename Hasher = hash<Key>, typename KeyEq = equal_to<Key>>
using HashSet = unordered_set<Key, Hasher, KeyEq, StlAllocator<Key>>;


