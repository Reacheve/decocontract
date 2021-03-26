#include <eosio/eosio.hpp>
#include <eosio/asset.hpp>
#include <eosio/system.hpp>
#include <eosio/time.hpp>
#include <eosio/singleton.hpp>

using namespace std;
using namespace eosio;

CONTRACT decocontract : public contract {
  public:
    using contract::contract;

    decocontract(name receiver, name code, datastream<const char*> ds) : contract(receiver, code, ds),
      _settings(receiver, receiver.value), _biders(receiver, receiver.value), _stakers(receiver, receiver.value), _registrations(receiver, receiver.value),
      _referrals(receiver, receiver.value), _tokens(receiver, receiver.value) {

        // Setting default value to settings table
        auto settings_stored = _settings.get_or_create( get_self(), default_settings );
        settings_stored.hodl_symbol = eosio::symbol("EOS", 4);
        settings_stored.stake_symbol = eosio::symbol("DECO", 4);
        settings_stored.apy = 5;
        settings_stored.max_bid_amount = 1000000;
        settings_stored.min_stake_days = 1;
        settings_stored.max_stake_days = 100;
        settings_stored.max_unwithdrawn_time = 100;
        settings_stored.percentage_share_to_distribute = 95;
        settings_stored.double_reward_time = 5;
        settings_stored.early_withdraw_penalty = 80;
        settings_stored.referral_percentage = 10;
        settings_stored.having_a_referral_percentage = 5;
        _settings.set(settings_stored, get_self());

      }
    
    ACTION registeruser(name user, uint32_t referral_id);
    
    // Inline action used to store the bid
    [[eosio::on_notify("eosio.token::transfer")]]
    void bid(name hodler, name to, eosio::asset quantity, std::string memo);

    // The inline action that receives the send asset and stores it for future claim
    [[eosio::on_notify("destinytoken::transfer")]]
    void stake(name staker, name to, eosio::asset quantity, std::string memo);

    // Reduce the amount of stake in pending state
    ACTION reducestake(name staker, eosio::asset quantity);

    // The action to claim the send tokens to stake
    ACTION setstake(name staker, int days);

    // The action to give divident for specific acount from the pool of bid tokens collected
    ACTION givedivident(uint32_t key);

    // The action to cancel the stake before maturity
    ACTION cancelstake(name staker, uint32_t key);

    // The action to withdraw the stake after maturity
    ACTION withdrawstake(name staker, uint32_t key);

    // The action to distribute the minted tokens and give dividend
    ACTION distanddiv(eosio::asset quantity);

    // The actions to clear all table
    ACTION clearbiders();
    ACTION clearstakers();
    ACTION cleartokens();
    ACTION clearregistr();
    ACTION clearrefs();
    ACTION clearall();

    // The action to set the settings variable
    ACTION setsettings(uint64_t apy, int max_bid_amount, int min_stake_days, int max_stake_days, uint64_t max_unwithdrawn_time, uint64_t percentage_share_to_distribute, int64_t double_reward_time, int early_withdraw_penalty, int referral_percentage, int having_a_referral_percentage);

  private:

    // Table to hold the settings of the smart contract
    TABLE settings {
      symbol hodl_symbol;
      symbol stake_symbol;
      uint64_t apy;
      int max_bid_amount;
      int min_stake_days;
      int max_stake_days;
      uint64_t max_unwithdrawn_time;
      uint64_t percentage_share_to_distribute;
      int64_t double_reward_time;
      int early_withdraw_penalty;
      int referral_percentage;
      int having_a_referral_percentage;
    } default_settings;
    typedef singleton<name("settings"),settings> settings_table;
    settings_table _settings;

    // Tabke to hold data about every bidder
    TABLE bider_info {
      name bidername;
      int64_t bid;
      string referrer;
      auto primary_key() const { return bidername.value; }
    };
    typedef eosio::multi_index<name("biderinfo"), bider_info> biders_table;
    biders_table _biders;

    // Table to hold information about every staker
    TABLE staker_info {
      uint32_t key;
      name staker;
      int64_t staked_amount;
      int staked_days;
      int days_passed;

      auto primary_key() const { return key; }
      uint64_t by_secondary() const { return staker.value; }
    };
    typedef multi_index<name("stakerinfo"), staker_info, eosio::indexed_by<name("secid"), eosio::const_mem_fun<staker_info, uint64_t, &staker_info::by_secondary>>> stakers_table;
    stakers_table _stakers;

    // Table to store accounts which send tokens but not yet staked
    TABLE tokens_info {
      name account_name;
      eosio::asset tokens;

      auto primary_key() const { return account_name.value; }
    };
    typedef multi_index<name("tokensinfo"), tokens_info> tokens_table;
    tokens_table _tokens;

    // Table to store registered users
    TABLE registration_info {
      uint32_t key;
      name registrant;

      auto primary_key() const { return key; }
      uint64_t by_secondary() const { return registrant.value; }
    };
    typedef multi_index<name("reginfo"), registration_info, eosio::indexed_by<name("secid"), eosio::const_mem_fun<registration_info, uint64_t, &registration_info::by_secondary>>> registration_table;
    registration_table _registrations;

    // Table to store all the referrals
    TABLE referral_info {
      name referred_person;
      name referrer;

      auto primary_key() const { return referred_person.value; }
      uint64_t by_secondary() const { return referrer.value; }
    };
    typedef multi_index<name("referralinfo"), referral_info, eosio::indexed_by<name("secid"), eosio::const_mem_fun<referral_info, uint64_t, &referral_info::by_secondary>>> referral_table;
    referral_table _referrals;

    TABLE messages {
      name    user;
      string  text;
      auto primary_key() const { return user.value; }
    };
    typedef multi_index<name("messages"), messages> messages_table;

    // Tokens to give for each bidded token
    int64_t per_token_rate(int64_t total_supply);

    // The percentage of bidded tokens to distribute
    int64_t total_bidded_tokens_to_distribute();

    // Calculate the total stake
    int64_t total_staked_tokens();

    // Calculate the interest to give
    int64_t interest_to_give(int64_t amt, int no_of_days, int maturity_days);

    // Distribute divident among the stakers
    void distdivident();

    // Distribute the daily set of tokens
    void distribute(eosio::asset quantity);



};








