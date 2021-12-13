#include "Memory/DramAnalyzer.hpp"
#include "Memory/DRAMAddr.hpp"

#include <cassert>
#include <unordered_set>
#include <immintrin.h>
#include <algorithm>
#include <iostream>

void DramAnalyzer::find_bank_conflicts() {
  size_t nr_banks_cur = 0;
  int remaining_tries = NUM_BANKS*256;  // experimentally determined, may be unprecise
  while (nr_banks_cur < NUM_BANKS && remaining_tries > 0) {
    reset:
    remaining_tries--;
    auto a1 = start_address + (dist(gen)%(MEM_SIZE/64))*64;
    auto a2 = start_address + (dist(gen)%(MEM_SIZE/64))*64;
    auto ret1 = measure_time(a1, a2);
    auto ret2 = measure_time(a1, a2);

    if ((ret1 > THRESH) && (ret2 > THRESH)) {
      bool all_banks_set = true;
      for (size_t i = 0; i < NUM_BANKS; i++) {
        if (banks.at(i).empty()) {
          all_banks_set = false;
        } else {
          auto bank = banks.at(i);
          ret1 = measure_time(a1, bank[0]);
          ret2 = measure_time(a2, bank[0]);
          if ((ret1 > THRESH) || (ret2 > THRESH)) {
            // possibly noise if only exactly one is true,
            // i.e., (ret1 > THRESH) or (ret2 > THRESH)
            goto reset;
          }
        }
      }

      // stop if we already determined addresses for each bank
      if (all_banks_set) return;

      // store addresses found for each bank
      assert(banks.at(nr_banks_cur).empty() && "Bank not empty");
      banks.at(nr_banks_cur).push_back(a1);
      banks.at(nr_banks_cur).push_back(a2);
      nr_banks_cur++;
    }
    if (remaining_tries==0) {
      Logger::log_error(format_string(
          "Could not find conflicting address sets. Is the number of banks (%d) defined correctly?",
          (int) NUM_BANKS));
      exit(1);
    }
  }

  Logger::log_info("Found bank conflicts.");
  for (auto &bank : banks) {
    find_targets(bank);
  }
  Logger::log_info("Populated addresses from different banks.");
}

void DramAnalyzer::find_targets(std::vector<volatile char *> &target_bank) {
  // create an unordered set of the addresses in the target bank for a quick lookup
  // std::unordered_set<volatile char*> tmp; tmp.insert(target_bank.begin(), target_bank.end());
  std::unordered_set<volatile char *> tmp(target_bank.begin(), target_bank.end());
  target_bank.clear();
  size_t num_repetitions = 5;
  while (tmp.size() < 10) {
    auto a1 = start_address + (dist(gen)%(MEM_SIZE/64))*64;
    if (tmp.count(a1) > 0) continue;
    uint64_t cumulative_times = 0;
    for (size_t i = 0; i < num_repetitions; i++) {
      for (const auto &addr : tmp) {
        cumulative_times += measure_time(a1, addr);
      }
    }
    cumulative_times /= num_repetitions;
    if ((cumulative_times/tmp.size()) > THRESH) {
      tmp.insert(a1);
      target_bank.push_back(a1);
    }
  }
}

DramAnalyzer::DramAnalyzer(volatile char *target) :
  row_function(0), start_address(target) {
  std::random_device rd;
  gen = std::mt19937(rd());
  dist = std::uniform_int_distribution<>(0, std::numeric_limits<int>::max());
  banks = std::vector<std::vector<volatile char *>>(NUM_BANKS, std::vector<volatile char *>());
}

std::vector<uint64_t> DramAnalyzer::get_bank_rank_functions() {
  return bank_rank_functions;
}

void DramAnalyzer::load_known_functions(int num_ranks) {
  if (num_ranks==1) {
    bank_rank_functions = std::vector<uint64_t>({0x2040, 0x24000, 0x48000, 0x90000});
    row_function = 0x3ffe0000;
  } else if (num_ranks==2) {
    bank_rank_functions = std::vector<uint64_t>({0x2040, 0x44000, 0x88000, 0x110000, 0x220000});
    row_function = 0x3ffc0000;
  } else {
    Logger::log_error("Cannot load bank/rank and row function if num_ranks is not 1 or 2.");
    exit(1);
  }

  Logger::log_info("Loaded bank/rank and row function:");
  Logger::log_data(format_string("Row function 0x%" PRIx64, row_function));
  std::stringstream ss;
  ss << "Bank/rank functions (" << bank_rank_functions.size() << "): ";
  for (auto bank_rank_function : bank_rank_functions) {
    ss << "0x" << std::hex << bank_rank_function << " ";
  }
  Logger::log_data(ss.str());
}

size_t DramAnalyzer::count_acts_per_ref() {
  size_t skip_first_N = 50;

  std::vector<uint64_t> a;
  std::vector<uint64_t> b;
  a.push_back((uint64_t) banks.at(0).at(0)); // bank 0, col 0
  b.push_back((uint64_t) banks.at(0).at(1)); // bank 0, col 1
  
  for(int i = 1; i < 4; i++) {
    DRAMAddr prev_a((void*) a[i - 1]);
    DRAMAddr prev_b((void*) b[i - 1]);

    DRAMAddr inc_a = prev_a.add(1, 0, 0);
    DRAMAddr inc_b = prev_b.add(1, 0, 0);
    
    a.push_back((uint64_t) inc_a.to_virt());
    b.push_back((uint64_t) inc_b.to_virt());
  }

  DRAMAddr testAddr((void*) a[0]);
  std::cout << testAddr.bank << std::endl;
  DRAMAddr testAddr2((void*) a[1]);
  std::cout << testAddr2.bank << std::endl;

  std::sort(a.begin(), a.end());
  std::sort(b.begin(), b.end());

  std::vector<uint64_t> acts;
  uint64_t running_sum = 0;
  uint64_t before, after, count = 0, count_old = 0;
  __m256 x;
  __m256 y;
  __m256d xd;
  __m256d yd;
  volatile double x1;
  volatile double y1;

  auto compute_std = [](std::vector<uint64_t> &values, uint64_t running_sum, size_t num_numbers) {
    double mean = static_cast<double>(running_sum)/static_cast<double>(num_numbers);
    double var = 0;
    for (const auto &num : values) {
      if (static_cast<double>(num) < mean) continue;
      var += std::pow(static_cast<double>(num) - mean, 2);
    }
    auto val = std::sqrt(var/static_cast<double>(num_numbers));
    return val;
  };

  __m256i idx_a = _mm256_set_epi64x(a[3] - a[0], a[2] - a[0], a[1] - a[0], 0);
  __m256i idx_b = _mm256_set_epi64x(b[3] - b[0], b[2] - b[0], b[1] - b[0], 0);

  Logger::log_info("sarting act test");

  for (size_t i = 0;; i++) {
    for(int j = 0; j < 4; j++) {
      clflushopt((volatile char*) a[j]);
      clflushopt((volatile char*) b[j]);
    }
    mfence();
    before = rdtscp();
    lfence();

    //(void)*a;
    //(void)*b;

    //x = _mm256_load_ps((const float *)a);
    //y = _mm256_load_ps((const float *)b);
    
    xd = _mm256_i64gather_pd((const float*)a[0], idx_a, 1);
    yd = _mm256_i64gather_pd((const float*)b[0], idx_b, 1);
    
    after = rdtscp();
    mfence();
    // write result to volatile variable so that compiler doesn't optimize away.
    x1 = x[0];
    y1 = y[0];
    double store_a[4] = {0, 0, 0, 0};
    double store_b[4] = {0, 0, 0, 0};
    _mm256_storeu_pd(store_a, xd);
    _mm256_storeu_pd(store_b, yd);
    x1 = store_a[0];
    y1 = store_b[0];

    count++;
    if ((after - before) > 1000) {
      if (i > skip_first_N && count_old!=0) {
        uint64_t value = (count - count_old)*2;
        acts.push_back(value);
        running_sum += value;
        // check after each 200 data points if our standard deviation reached 1 -> then stop collecting measurements
        if ((acts.size()%200)==0 && compute_std(acts, running_sum, acts.size())<3.0) break;
      }
      count_old = count;
    }
  }

  auto activations = (running_sum/acts.size());
  Logger::log_info("Determined the number of possible ACTs per refresh interval.");
  Logger::log_data(format_string("num_acts_per_tREFI: %lu", activations));

  //exit(0);

  return activations;
}
