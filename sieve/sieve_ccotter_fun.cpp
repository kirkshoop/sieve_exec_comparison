/**
 * @file sieve_unifex_fun.cpp
 *
 * @section LICENSE
 *
 * The MIT License
 *
 * @copyright Copyright (c) 2022 TileDB, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 * @section DESCRIPTION
 *
 * Demo program for libunifex: Sieve of Eratosthenes,
 * free function version.
 */

#include <atomic>
#include <cmath>
#include <functional>


#include <unifex/async_scope.hpp>
#include <unifex/on.hpp>
#include <unifex/scheduler_concepts.hpp>
#include <unifex/sync_wait.hpp>
#include <unifex/static_thread_pool.hpp>
#include <unifex/then.hpp>
#include <unifex/when_all.hpp>
#include <unifex/just.hpp>

#include "sieve.hpp"
#include "sieve_fun.hpp"
#include "timer.hpp"

using namespace std::placeholders;

/**
 * @brief Generate primes from 2 to n using sieve of Eratosthenes.
 * @tparam bool_t the type to use for the "bitmap"
 * @param n upper bound of sieve
 * @param block_size how many primes to search for given a base set of primes
 */
template <class bool_t>
auto sieve_unifex_block(size_t n, size_t block_size) {
  size_t sqrt_n = static_cast<size_t>(std::ceil(std::sqrt(n)));

  /* Generate base set of sqrt(n) primes to be used for subsequent sieving */
  auto first_sieve = sieve_seq<bool_t>(sqrt_n);
  std::vector<size_t> base_primes = sieve_to_primes(first_sieve);

  /* Store vector of list of primes (each list generated by separate task chain) */
  std::vector<std::shared_ptr<std::vector<size_t>>> prime_list(n / block_size + 2);
  prime_list[0] = std::make_shared<std::vector<size_t>>(base_primes);

  namespace ex = unifex;

  unifex::static_thread_pool pool{std::thread::hardware_concurrency()};
  ex::async_scope scope;

  /**
   * Build pipeline
   */
  auto bod = input_body{};
  auto l = [&bod]() mutable { return bod(); };
  
  auto make_snd = [&]() {

    ex::sender auto snd =
      ex::just() |
      ex::then(l) |
      ex::then(std::bind(gen_range<bool_t>, _1, block_size, sqrt_n, n)) |
      ex::then(std::bind(range_sieve<bool_t>, _1, cref(base_primes))) |
      ex::then(sieve_to_primes_part<bool_t>) |
      ex::then(std::bind(output_body, _1, ref(prime_list)));
    
    return snd;
  };

#if 0

  /* launch tasks on async_scope */
  for (size_t i = 0; i < n / block_size + 1; ++i) {
    scope.spawn_on(pool.get_scheduler(), make_snd());
  }

  /* wait for tasks to finish */
  ex::sync_wait(scope.complete());

#else

  std::vector<decltype(make_snd())> D;
  for (size_t i = 0; i < n / block_size + 1; ++i) {
    D.emplace_back(make_snd());
  }

  auto wa = ex::when_all2(begin(D), end(D));
  auto res = ex::sync_wait(std::move(wa));

#endif

  return prime_list;
}


int main(int argc, char* argv[]) {
  size_t number = 100'000'000;
  size_t block_size = 100;

  if (argc >= 2) {
    number = std::stol(argv[1]);
  }
  if (argc >= 3) {
    block_size = std::stol(argv[2]);
  }

  auto using_bool_unifex_block = timer_2(sieve_unifex_block<bool>, number, block_size * 1024);
  auto using_char_unifex_block = timer_2(sieve_unifex_block<char>, number, block_size * 1024);

  std::cout << "Time using bool unifex block: " << duration_cast<std::chrono::milliseconds>(using_bool_unifex_block).count() << "\n";
  std::cout << "Time using char unifex block: " << duration_cast<std::chrono::milliseconds>(using_char_unifex_block).count() << "\n";

  return 0;
}
