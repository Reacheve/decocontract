#include <decocontract.hpp>

int64_t decocontract::total_bidded_tokens_to_distribute() {

  int64_t total_token_received = 0;

  auto iterator = _biders.begin();
  while(iterator != _biders.end()) {
    total_token_received = total_token_received + (iterator->bid);
    iterator++;
  }

  int64_t tokens_to_distribute = (_config.get().percentage_share_to_distribute * total_token_received) / 100;

  return tokens_to_distribute;
}

int64_t decocontract::total_staked_tokens() {

  int64_t total_staked_tks = 0;

  auto iterator = _stakers.begin();
  while(iterator != _stakers.end()) {
    if(iterator->days_passed > 0) {
      total_staked_tks = total_staked_tks + iterator->staked_amount;
    }

    iterator++;
  }

  return total_staked_tks;
}

int64_t decocontract::interest_to_give(int64_t amt, int no_of_days, int maturity_days) {

  if(no_of_days > maturity_days)
    no_of_days = maturity_days;

  int64_t interest = (amt * _config.get().apy * no_of_days) / (365 * 100);

  // The interest double at regular time interval
  int64_t extra_interest = ((no_of_days / _config.get().double_reward_time) * _config.get().apy * amt) / (365 * 100);

  return (interest + extra_interest);
}

void decocontract::distdivident() {

  require_auth(get_self());

  auto iterator = _stakers.begin();
  int64_t t_staked_tokens = total_staked_tokens();
  int64_t t_bidded_tokens_to_distribute = total_bidded_tokens_to_distribute();

  while(iterator != _stakers.end()) {
  
    if((iterator->days_passed > 0) && (iterator->days_passed <=  iterator->staked_days) && (t_staked_tokens > 0)) {

      int64_t tokens_to_give = ((iterator->staked_amount) * t_bidded_tokens_to_distribute) / t_staked_tokens;

      if(tokens_to_give > 0) {
      
        action {
          permission_level(get_self(), "active"_n),
          "eosio.token"_n,
          "transfer"_n,
          std::make_tuple(
            get_self(),
            iterator->staker,
            eosio::asset(tokens_to_give, _config.get().hodl_symbol),
            std::string("Giving Divident")
          )
        }.send();
      }
    }

    // Clear the records after max_unwithdrawn_time
    if(iterator->days_passed > (iterator->staked_days + _config.get().max_unwithdrawn_time))
      _stakers.erase(iterator);
    else {
    
      _stakers.modify(iterator, get_self(), [&](auto& row){
        row.staker = row.staker;
        row.staked_amount = row.staked_amount;
        row.staked_days = row.staked_days;
        row.days_passed = row.days_passed + 1;
      });
    }

    iterator++;
  }
}

void decocontract::distribute(eosio::asset quantity) {

  // Only the account containing the contract can distribute
  require_auth(get_self());

  // Calculate the total bid
  int64_t total_bid = 0;

  auto itr = _biders.begin();
  while(itr != _biders.end()) {
    total_bid = total_bid + (itr->bid);
    itr++;
  }

  auto iterator = _biders.begin();
  while(iterator != _biders.end()) {

    int64_t tokens_to_send = (iterator->bid * quantity.amount)/total_bid;
    check(tokens_to_send > 0, "tokens to send is not greater than 0");
    
    int64_t referral_share = (_config.get().referral_percentage * tokens_to_send) / 100;

    if((iterator->referrer.length() > 0) && (referral_share > 0)) {
    
      name receiver = eosio::name(iterator->referrer);
      action {
        permission_level(get_self(), "active"_n),
        "destinytoken"_n,
        "transfer"_n,
        std::make_tuple(
          get_self(),
          receiver,
          eosio::asset(referral_share, quantity.symbol),
          std::string("Commission for Referring")
        )
      }.send();

      // Calculating the extra commission for having a referrer
      int64_t extra = (_config.get().having_a_referral_percentage * tokens_to_send) / 100;
      tokens_to_send = tokens_to_send + extra;
    }

    if(tokens_to_send > 0) {
      action {
        permission_level(get_self(), "active"_n),
        "destinytoken"_n,
        "transfer"_n,
        std::make_tuple(
          get_self(),
          iterator->bidername,
          eosio::asset(tokens_to_send, quantity.symbol),
          std::string("Delegated Tokens + Bonus if any")
        )
      }.send(); 
    }

    iterator = _biders.erase(iterator);
  }
}

ACTION decocontract::registeruser(name user, uint32_t referral_id) {
  require_auth(user);

  check(_config.get().freeze_level == 0, "contract under freeze for maintainance");

  // Account can be registered only once
  auto reg = _registrations.get_index<name("secid")>();
  auto reg_itr = reg.find(user.value);
  check(reg_itr == reg.end(), "account already registered");

  auto iterator = _registrations.find(referral_id);

  if(referral_id > 0) {
    check(iterator != _registrations.end(), "Wrong referal id");

    _referrals.emplace(get_self(), [&](auto& row){
      row.referred_person = user;
      row.referrer = iterator->registrant;
    });
  }

  uint32_t key = current_time_point().sec_since_epoch();
  _registrations.emplace(get_self(), [&](auto& row){
    row.key = key;
    row.registrant = user;
  });
}

[[eosio::on_notify("*::transfer")]]
void decocontract::bid(name hodler, name to, eosio::asset quantity, std::string memo) {

  // Ignore when the smart contract is sending tokens and the memo is IGNORE_THIS
  if(hodler == get_self() || memo == "IGNORE_THIS" || memo == "Jungle Faucet")
    return;

  check(_config.get().freeze_level == 0, "contract under freeze for maintainance");

  check(_config.get().hodl_contract == get_first_receiver(), "This contract is not accepted for bidding");

  check(quantity.amount > 0, "quantity must be greater than 0");
  check(quantity.amount <= _config.get().max_bid_amount, "more than max bid limit");
  check(quantity.symbol == _config.get().hodl_symbol, "cant bid with this token");

  // Only registered account can bid
  auto reg = _registrations.get_index<name("secid")>();
  auto reg_itr = reg.find(hodler.value);
  check(reg_itr != reg.end(), "account is not registered");

  string referrer_account = "";
  auto ref = _referrals.find(hodler.value);

  if(ref != _referrals.end())
    referrer_account = ref->referrer.to_string();

  auto iterator = _biders.find(hodler.value);

  if(iterator == _biders.end()) {
    // No previous bid record
    _biders.emplace(get_self(), [&](auto& row){
      row.bidername = hodler;
      row.bid = quantity.amount;
      row.referrer = referrer_account;
    });
  } else {
    // Previous bid was made
    _biders.modify(iterator, get_self(), [&](auto& row){
      row.bidername = hodler;
      row.bid = row.bid + quantity.amount;
      row.referrer = referrer_account;
    });
  }
}

[[eosio::on_notify("*::transfer")]]
void decocontract::stake(name staker, name to, eosio::asset quantity, std::string memo) {

  // Ignore when the contract is sending the tokens or the memo is IGNORE_THIS
  if(staker == get_self() || memo == "IGNORE_THIS")
    return;

  check(_config.get().freeze_level == 0, "contract under freeze for maintainance");

  check(_config.get().stake_contract == get_first_receiver(), "This contract is not accepted for staking");

  // Only registered account can stake
  auto reg = _registrations.get_index<name("secid")>();
  auto reg_itr = reg.find(staker.value);
  check(reg_itr != reg.end(), "account is not registered");

  check(quantity.amount > 0, "staked amount must be greater than 0");
  check(quantity.symbol == _config.get().stake_symbol, "this token is not accepted for staking");

  auto iterator = _tokens.find(staker.value);
  if(iterator == _tokens.end()) {
    _tokens.emplace(get_self(), [&](auto& row){
      row.account_name = staker;
      row.tokens = quantity;
    });
  } else {
    _tokens.modify(iterator, get_self(), [&](auto& row){
      row.account_name = row.account_name;
      row.tokens = eosio::asset((row.tokens.amount + quantity.amount), row.tokens.symbol);
    });
  }
}

ACTION decocontract::reducestake(name staker, eosio::asset quantity) {

  require_auth(staker);

  check(_config.get().freeze_level == 0, "contract under freeze for maintainance");

  auto iterator = _tokens.find(staker.value);

  check(iterator != _tokens.end(), "No pending stake to reduce from");
  eosio::asset tk = iterator->tokens;
  check(tk.symbol == quantity.symbol, "Symbol doesnt match with staked tokens");
  check(tk.amount >= quantity.amount, "Can't withdraw more than that in pending state");

  if(tk.amount > quantity.amount) {
    _tokens.modify(iterator, get_self(), [&](auto& row){
      row.account_name = row.account_name;
      row.tokens = eosio::asset((row.tokens.amount - quantity.amount), row.tokens.symbol);
    });
  } else if(tk.amount == quantity.amount) {
    _tokens.erase(iterator);
  }

  action {
    permission_level(get_self(), "active"_n),
    "destinytoken"_n,
    "transfer"_n,
    std::make_tuple(
      get_self(),
      staker,
      quantity,
      std::string("Withdrawing tokens from pending stake")
    )
  }.send();
}

ACTION decocontract::setstake(name staker, int days) {

  require_auth(staker);

  check(_config.get().freeze_level == 0, "contract under freeze for maintainance");

  check(days >= _config.get().min_stake_days, "Staking days is less the minimum staking period");
  check(days < _config.get().max_stake_days, "Staking days is more that maximum staking period");

  auto iterator = _tokens.find(staker.value);
  check(iterator != _tokens.end(), "No pending stake is found");

  eosio::asset tk = iterator->tokens;
  int64_t amt = tk.amount;

  _stakers.emplace(get_self(), [&](auto& row){
    row.key = current_time_point().sec_since_epoch();
    row.staker = staker;
    row.staked_amount = amt;
    row.staked_days = days;
    row.days_passed = 0;
  });

  _tokens.erase(iterator);
}

ACTION decocontract::givedivident(uint32_t key) {

  // Only the account owning the contract can give the divident
  require_auth(get_self());

  check(_config.get().freeze_level == 0, "contract under freeze for maintainance");

  auto iterator = _stakers.find(key);
  check(iterator != _stakers.end(), "the given key is not in the stakers table");

  if((iterator->days_passed > 0) && (iterator->days_passed <= iterator->staked_days)) {

    int64_t tokens_to_give = (((iterator->staked_amount) * total_bidded_tokens_to_distribute()) / (total_staked_tokens()));

    if(tokens_to_give > 0) {
      action {
        permission_level(get_self(), "active"_n),
        "eosio.token"_n,
        "transfer"_n,
        std::make_tuple(
          get_self(),
          iterator->staker,
          eosio::asset(tokens_to_give, _config.get().hodl_symbol),
          std::string("Giveing Divident")
        )
      }.send();
    }
  }

  if(iterator->days_passed > iterator->staked_days + _config.get().max_unwithdrawn_time) {
    _stakers.erase(iterator);
    return;
  }

  _stakers.modify(iterator, get_self(), [&](auto& row){
    row.staker = row.staker;
    row.staked_amount = row.staked_amount;
    row.staked_days = row.staked_days;
    row.days_passed = row.days_passed + 1;
  });
}

ACTION decocontract::cancelstake(name staker, uint32_t key) {

  require_auth(staker);

  check(_config.get().freeze_level == 0, "contract under freeze for maintainance");

  auto iterator = _stakers.find(key);
  check(iterator != _stakers.end(), "the given key is not in the stakers table");
  check(iterator->staker == staker, "the account name doesn't match with the staker name");
  check(iterator->staked_days >= iterator->days_passed, "account is matured and can be withdrawn");

  // They are penalized for early withdrawal
  int64_t amt_to_give = (((100 - _config.get().early_withdraw_penalty) * iterator->staked_amount) / 100) + interest_to_give(iterator->staked_amount, iterator->days_passed, iterator->staked_days);

  check(amt_to_give > 0, "No token to withdraw");

  action {
    permission_level(get_self(), "active"_n),
    "destinytoken"_n,
    "transfer"_n,
    std::make_tuple(
      get_self(),
      staker,
      eosio::asset(amt_to_give, _config.get().stake_symbol),
      std::string("Premature Withdraw of Stake")
    )
  }.send();

  _stakers.erase(iterator);
}

ACTION decocontract::withdrawstake(name staker, uint32_t key) {

  require_auth(staker);

  check(_config.get().freeze_level == 0, "contract under freeze for maintainance");

  auto iterator = _stakers.find(key);
  check(iterator != _stakers.end(), "the given key is not in the stakers table");
  check(iterator->staker == staker, "the account name doesn't match with the staker name");
  check(iterator->staked_days < iterator->days_passed, "stake still not matured");

  int64_t amt_to_give = iterator->staked_amount + interest_to_give(iterator->staked_amount, iterator->days_passed, iterator->staked_days);

  check(amt_to_give > 0, "no token to withdraw");

  action {
    permission_level(get_self(), "active"_n),
    "destinytoken"_n,
    "transfer"_n,
    std::make_tuple(
      get_self(),
      staker,
      eosio::asset(amt_to_give, _config.get().stake_symbol),
      std::string("Withdraw stake with interest")
    )
  }.send();

  _stakers.erase(iterator);
}

ACTION decocontract::distanddiv(eosio::asset supply) {

  require_auth(get_self());

  check(_config.get().freeze_level == 0, "contract under freeze for maintainance");
  
  distdivident();
  distribute(supply);

}

ACTION decocontract::clearbiders() {
  
  require_auth(get_self());

  auto iterator = _biders.begin();
  while(iterator != _biders.end())
    iterator = _biders.erase(iterator);
}

ACTION decocontract::clearstakers() {
  
  require_auth(get_self());
  
  auto iterator = _stakers.begin();
  while(iterator != _stakers.end())
    iterator = _stakers.erase(iterator);
}

ACTION decocontract::cleartokens() {
  
  require_auth(get_self());
  
  auto iterator = _tokens.begin();
  while(iterator != _tokens.end())
    iterator = _tokens.erase(iterator);
}

ACTION decocontract::clearregistr() {
  
  require_auth(get_self());
  
  auto iterator = _registrations.begin();
  while(iterator != _registrations.end())
    iterator = _registrations.erase(iterator);
}

ACTION decocontract::clearrefs() {
  
  require_auth(get_self());
  
  auto iterator = _referrals.begin();
  while(iterator != _referrals.end())
    iterator = _referrals.erase(iterator);
}

ACTION decocontract::clearall() {

  // Only the account containing the contract can call the clearall action
  require_auth(get_self());

  clearbiders();
  clearstakers();
  cleartokens();
  clearregistr();
  clearrefs();
}

ACTION decocontract::setconfig(string hodl_symbol, uint8_t hodl_precision, name hodl_contract,
      string stake_symbol, uint8_t stake_precision, name stake_contract,
      uint64_t apy, uint64_t max_bid_amount, int min_stake_days, int max_stake_days,
      uint64_t max_unwithdrawn_time, uint64_t percentage_share_to_distribute,
      int64_t double_reward_time, int early_withdraw_penalty, int referral_percentage, int having_a_referral_percentage) {
  
  require_auth(get_self());

  auto config_stored = _config.get_or_create( get_self(), default_config );
  config_stored.hodl_symbol = eosio::symbol(hodl_symbol, hodl_precision);
  config_stored.hodl_contract = hodl_contract;
  config_stored.stake_symbol = eosio::symbol(stake_symbol, stake_precision);
  config_stored.stake_contract = stake_contract;
  config_stored.apy = apy;
  config_stored.max_bid_amount = max_bid_amount;
  config_stored.min_stake_days = min_stake_days;
  config_stored.max_stake_days = max_stake_days;
  config_stored.max_unwithdrawn_time = max_unwithdrawn_time;
  config_stored.percentage_share_to_distribute = percentage_share_to_distribute;
  config_stored.double_reward_time = double_reward_time;
  config_stored.early_withdraw_penalty = early_withdraw_penalty;
  config_stored.referral_percentage = referral_percentage;
  config_stored.having_a_referral_percentage = having_a_referral_percentage;
  _config.set(config_stored, get_self());
 
}

ACTION decocontract::setfreeze(int freeze_level) {
  require_auth(get_self());

  auto configs_stored = _config.get_or_create( get_self(), default_config );
  configs_stored.freeze_level = freeze_level;
  _config.set(configs_stored, get_self());

}

ACTION decocontract::init() {

  require_auth(get_self());

  // Setting default value to settings table
  auto config_stored = _config.get_or_create( get_self(), default_config );
  config_stored.hodl_symbol = eosio::symbol("EOS", 4);
  config_stored.hodl_contract = eosio::name("eosio.token");
  config_stored.stake_symbol = eosio::symbol("DECO", 4);
  config_stored.stake_contract = eosio::name("destinytoken");
  config_stored.apy = 5;
  config_stored.max_bid_amount = 1000000;
  config_stored.min_stake_days = 1;
  config_stored.max_stake_days = 100;
  config_stored.max_unwithdrawn_time = 100;
  config_stored.percentage_share_to_distribute = 95;
  config_stored.double_reward_time = 5;
  config_stored.early_withdraw_penalty = 80;
  config_stored.referral_percentage = 10;
  config_stored.having_a_referral_percentage = 5;
  config_stored.freeze_level = 0;
  _config.set(config_stored, get_self());


}

// EOSIO_DISPATCH(decocontract, (registeruser)(reducestake)(setstake)(givedivident)(cancelstake)(withdrawstake)(distanddiv)(clearbiders)(clearstakers)(cleartokens)(clearregistr)(clearrefs)(clearall)(setsettings))












