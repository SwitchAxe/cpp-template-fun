#include <type_traits>
#include <functional>
#include <map>
#include <tuple>
#include <algorithm>
#include <ranges>
#include <stdexcept>
#include <iostream>
#include <variant>
#include <utility>
#include <string>
#include <list>
#include <optional>

using namespace std::placeholders;

// this is VERY useful
template<class... Ts> struct overloaded : Ts... { using Ts::operator()...; };
template<class... Ts> overloaded(Ts...) -> overloaded<Ts...>;

namespace Functional {
	
  namespace Utility {
    // if we get a variant, call the function contained in the variant.
    // otherwise, just call the function.
		
    template <class Ret, class T> Ret call(T func, auto... args) {
      return func(args...);
    }

    template <class Ret, class T> Ret call(T func, std::monostate) {
      throw std::logic_error {"Monostate, shouldn't happen\n"};
    }

    template <class Ret, class T> Ret call(std::monostate, T) {
      throw std::logic_error {"Monostate, shoudln't happen\n"};
    }

    template <class Ret> Ret call(std::monostate, std::monostate) {
      throw std::logic_error {"Monostate, shoudln't happen\n"};
    }

    template <class Ret, class T, class U, class V>
    Ret call(T func, std::pair<U, V> p) {
      auto [a, b] = p;
      return call<Ret>(func, a, b);
    }

    template <class Ret, class... Ts>
    Ret call(std::variant<Ts...> func, auto... args) {
      return std::visit([&]<class F>(F f) -> Ret {
	if constexpr (std::is_same_v<F, std::monostate>) {
	  throw std::logic_error {"Monostate, shouldn't happen\n"};
	}
	return f(args...);
      }, func);
    }
  };

  namespace Types {
    struct Placeholder {
      constexpr auto operator<=>(const Placeholder& other) const = default;
    };

    struct Match { constexpr auto operator<=>(const Match&) const = default; };
    static constexpr Match _ = Match{};

    struct ListTailMatch {
      constexpr auto operator<=>(const ListTailMatch&) const = default;
    };

    static constexpr ListTailMatch __ = ListTailMatch{};

    struct SingleMatch {
      constexpr auto operator<=>(const SingleMatch&) const = default;
    };


    struct HeadTailMatch {
      constexpr auto operator<=>(const HeadTailMatch&) const = default;
    };

    struct TupleMatch {
      constexpr auto operator<=>(const TupleMatch&) const = default;
    };

    template <class T> struct List {
      std::list<T> elements;
      T head;
      std::list<T> tail;
      List(std::initializer_list<T> l) : elements(l) {
	head = elements.front();
	std::copy(l.begin() + 1, l.end(), std::back_inserter(tail));
      }

      List(T h, std::list<T> t) {
	head = h;
	tail = t;
	elements = {h};
	elements.insert(elements.end(), t);
      }
      List(std::list<T> t) {
	elements = t;
	head = t.front();
	t.pop_front();
	tail = t;
      }

      constexpr auto operator+(List<T> other) {
	tail.insert(tail.end(), other.tail);
	elements.insert(elements.end(), other.elements);
      }

      constexpr auto operator<=>(const List<T>& l) const = default;
    };
  }

  namespace Match {
    using namespace Types;

    template <class... Ts> struct PatternMatchEntry {
      using pattern = std::variant<std::monostate,
				   typename PatternMatchEntry<Ts>::pattern...>;
    };

    template <class T> struct PatternMatchEntry<T> {
      using pattern = std::variant<Types::Match, T>;
    };

    template <class T> struct PatternMatchEntry<List<T>> {
      using pattern = std::variant<std::monostate,
				   List<T>,
				   SingleMatch,
				   HeadTailMatch,
				   Types::Match>;
    };

    HeadTailMatch list(Types::Match a, Types::Match b) { return HeadTailMatch{}; }
    SingleMatch list(Types::Match a) { return SingleMatch{}; }
  }

  namespace Functor {
    using namespace Types;
    using namespace Match;
    template <class Ret, class... Ts> struct Functor {
      using signature =
				std::function<Ret(typename PatternMatchEntry<Ts>::pattern...)>;
    };

    template <class Ret, class T> struct Functor<Ret, T> {
      using signature = std::function<Ret(T)>;
    };

    template <class Ret, class T> struct Functor<Ret, List<T>> {
      using signature = std::variant<std::function<Ret(List<T>)>,
				     std::function<Ret(T, List<T>)>,
				     std::function<Ret(T)>>;
    };

  }

  namespace Visit {
    using namespace Match;
    using namespace Types;
    using namespace Functor;

    template <class T> size_t rank(T) { return 0; }

    template <class... Ts> size_t rank(std::tuple<Ts...> tree) {
      return std::apply([](auto... x) { return (rank(x) + ...); }, tree);
    }

    template <class... Ts> size_t rank(std::variant<Ts...> var) {
      return std::visit([](auto x) { return rank(x); }, var);
    }

    template <> size_t rank(SingleMatch)   { return 1; }
    template <> size_t rank(HeadTailMatch) { return 2; }
    template <> size_t rank(Types::Match)  { return 1; }

    template <class K, class V> std::vector<K> sort_rank(std::map<K, V> m) {
      std::vector<K> sorted;
      for (typename std::map<K, V>::iterator it = m.begin(); it != m.end(); ++it)
	sorted.push_back(it->first);
      const auto pred = [](auto a, auto b) { return rank(a) < rank(b); };
      std::sort(sorted.begin(), sorted.end(), pred);
      return sorted;
    }

    template <class T, class U>
    std::optional<U> visit(T, U) { return std::nullopt; }

    template <class T>
    std::optional<T> visit(T expected, T got) {
      if (expected == got) return got;
      return std::nullopt; }

    template <class T> std::optional<T>
    visit(Types::Match expected, T got) { return got; }

    template <class T>
    std::optional<T>
    visit(Types::SingleMatch expected, List<T> got) {
      if (got.elements.size() == 1) return got.head;
      return std::nullopt;
    }

    template <class T>
    std::optional<std::pair<T, List<T>>>
    visit(Types::HeadTailMatch expected, List<T> got) {
      if (got.elements.size() > 0)
	return std::pair{got.head, List<T>(got.tail)};
      return std::nullopt;
    }

    template <class... Ts>
    auto visit(std::tuple<Ts...> expected, std::tuple<Ts...> got) {
      auto idx_seq =
	std::make_index_sequence<std::tuple_size_v<std::tuple<Ts...>>>();

      auto zip = [&]<size_t... Is>(std::index_sequence<Is...>) {
	return std::make_tuple((std::pair{std::get<Is>(expected)},
				std::pair{std::get<Is>(got)})...);
      };
      auto zipped = zip(idx_seq);

      auto visited =
	std::apply([](auto... p) {
	  return std::make_tuple(visit(p.first, p.second)...);
	}, zipped);

      bool not_correct =
	std::apply([](auto... x) {
	  return ((x == std::nullopt) || ...);
	}, visited);

      if (not_correct) return std::nullopt;
      return std::apply([](auto... x) {
	return std::make_tuple((*x)...);
      }, visited);
    }
  }

  namespace Define {
    using namespace Types;
    using namespace Match;
    template <class Ret, class... Ts> struct Function {
      using signature = std::tuple<typename Function<Ret, Ts>::signature...>;
      signature pattern;
      using function_signature =
        std::function<Ret(typename Function<Ret, Ts>::signature...)>;
      std::map<signature, function_signature> cache;

      constexpr void operator=(function_signature f) {
	cache.insert(std::make_pair(pattern, f));
	pattern = {};
      }

      constexpr auto
      define(auto... args)
          -> std::add_lvalue_reference_t<std::remove_pointer_t<decltype(this)>> {
	pattern = std::make_tuple(Function<Ret, Ts>(args).pattern...);
	return *this;
      }
	
      constexpr Ret operator()(Ts... args) {
	auto l = Visit::sort_rank(cache);
	for (auto k : l) {
	  auto result =
	    std::visit([&]<class U>(U x) {
	      return Visit::visit(x, std::make_tuple(args...));
	    }, k);
	  if (result != std::nullopt) return cache[k](*result);
	}
	throw std::logic_error{"Error"};
      }
    };

    template <class Ret, class T> struct Function<Ret, T> {
      using signature = PatternMatchEntry<T>::pattern;
      using function_signature = Functor::Functor<Ret, T>::signature;
      signature pattern;
      std::map<signature, function_signature> cache;

      constexpr Ret operator()(T arg) {
	auto l = Visit::sort_rank(cache);
	for (auto k : l) {
	  auto result =
	    std::visit([&]<class U>(U x) {
	      return Visit::visit(x, arg);
	    }, k);
	  if (result != std::nullopt)
	    return Utility::call<Ret>(cache[k], *result);
	}
	throw std::logic_error{"Error"};
      }

      constexpr void operator=(function_signature f) {
	cache.insert(std::make_pair(pattern, f));
	pattern = {};
      }

      constexpr auto
      define(auto arg)
	  -> std::add_lvalue_reference_t<std::remove_pointer_t<decltype(this)>> {
	pattern = arg;
	return *this;
      }
    };

    // some specializations to make my life easier...

    // lists (custom Lists, that is)
    template <class Ret, class T> struct Function<Ret, List<T>> {
      using signature = PatternMatchEntry<List<T>>::pattern;
      using function_signature = Functor::Functor<Ret, List<T>>::signature;
      signature pattern;
      std::map<signature, function_signature> cache;
      using possible_destructures = std::variant<std::monostate,
                                                                              std::pair<T, List<T>>,
						                              List<T>,
						                              T>;
      constexpr Ret operator()(List<T> arg) {
	auto l = Visit::sort_rank(cache);
	possible_destructures got;
	for (auto k : l) {
	  auto opt =
	    std::visit([&](auto x) -> std::optional<possible_destructures> {
	      return Visit::visit(x, arg);
	    }, k);
	  if (opt != std::nullopt) {
	    got = *opt;
	    return std::visit(overloaded{
	      [&](std::pair<T, Types::List<T>> p,
		  std::function<Ret(T, List<T>)> f) -> Ret {
		return f(p.first, p.second);
	      },
	      [&](List<T> l, std::function<Ret(List<T>)> f) -> Ret {
		return f(l);
	      },
	      [&](T x, std::function<Ret(T)> f) -> Ret {
		return f(x);
	      },
	      [&](auto, auto) -> Ret {
		throw std::logic_error {"Error\n"};
	      }
	    }, got, cache[k]);
	  }
	}
	throw std::logic_error{"Error"};
      }

      constexpr void operator=(function_signature f) {
	cache.insert(std::make_pair(pattern, f));
	pattern = {};
      }

      constexpr auto
      define(auto arg)
	  -> std::add_lvalue_reference_t<std::remove_pointer_t<decltype(this)>> {
	pattern = arg;
	return *this;
      }

    };

  }
}

int main() {
  using namespace Functional::Define;
  using namespace Functional::Types;

  Function<size_t, std::string> str_len;
  str_len.define("") = [](std::string s) -> size_t { return 0; };
  str_len.define(_) = [&](std::string s) -> size_t { s.erase(s.begin());
    return 1 + str_len(s); };

  Function<size_t, List<std::string>> list_len;
  list_len.define(list(_)) = [](std::string s) -> size_t { return 1; };
  list_len.define(list(_, _)) = [&](std::string h, List<std::string> t) -> size_t {
    return 1 + list_len(t);
  };

  std::cout << str_len("ciao") << "\n";
  std::cout << list_len({"a", "b", "c"}) << "\n";
}
