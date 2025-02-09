#include <imtjson/value.h>
#include "backtest.h"

#include <cmath>

#include "../imtjson/src/imtjson/object.h"
#include "istatsvc.h"
#include "mtrader.h"
#include "sgn.h"

using TradeRec=IStatSvc::TradeRecord;
using Trade=IStockApi::Trade;
using Ticker=IStockApi::Ticker;

BTTrades backtest_cycle(const MTrader_Config &cfg, BTPriceSource &&priceSource, const IStockApi::MarketInfo &minfo, std::optional<double> init_pos, double balance, bool neg_bal, bool spend) {

	BTTrades trades;
	try {
		std::optional<BTPrice> price = priceSource();
		if (!price.has_value()) return trades;

		Strategy s = cfg.strategy;

		BTTrade bt;
		bt.price = *price;

		double pos;
		if (init_pos.has_value() ) {
			pos = *init_pos;
			if (minfo.invert_price) pos = -pos;
		}else {
			pos = s.calcInitialPosition(minfo,bt.price.price,0,balance);
			if (!minfo.leverage) balance -= pos * bt.price.price;
		}

		trades.push_back(bt);

		double total_spend = 0;
		double pl = 0;
		for (price = priceSource();price.has_value();price = priceSource()) {
			double minsize = std::max(minfo.min_size, cfg.min_size);
			if (std::abs(price->price-bt.price.price) == 0) continue;
			bt.event = BTEvent::no_event;
			double p = price->price;
			Ticker tk{p,p,p,price->time};
			double prev_bal = balance;
			bool enable_alert = true;

			double eq = s.getCenterPrice(bt.price.price,pos);
			double dir = p>eq?-1:1;
			s.onIdle(minfo,tk,pos,balance);
			double adjbal = std::max(balance,0.0);
			Strategy::OrderData order = s.getNewOrder(minfo, bt.price.price*0.9+p*0.1, p, dir, pos, adjbal,false);

			if (order.price) {
				p = order.price;
			}

			double dprice = (p - bt.price.price);
			double pchange = pos * dprice;
			pl = pl + pchange;
			if (minfo.leverage) balance += pchange;

			order.size  = IStockApi::MarketInfo::adjValue(order.size,minfo.asset_step,round);
			if (cfg.max_balance.has_value()) {
				if (pos > *cfg.max_balance) order.size = 0;
				else if (order.size + pos > *cfg.max_balance) order.size = *cfg.max_balance - pos;
			}
			if (cfg.min_balance.has_value()) {
				if (pos < *cfg.max_balance) order.size = 0;
				else if (order.size + pos < *cfg.max_balance) order.size = *cfg.min_balance - pos;
			}
			if (minfo.leverage) {
				double max_lev = cfg.max_leverage?std::min(cfg.max_leverage,minfo.leverage):minfo.leverage;
				double max_abs_pos = (adjbal * max_lev)/bt.price.price;
				double new_pos = std::abs(pos + order.size);
				double cur_pos = std::abs(pos);
				if (new_pos > cur_pos && new_pos > max_abs_pos) {
					if (cfg.accept_loss) {
						s.reset();
						s.onIdle(minfo, tk, pos, adjbal);
						bt.event = BTEvent::accept_loss;
					} else {
						bt.event = BTEvent::margin_call;
					}
					order.size = 0;
				}
			}
			if (minfo.min_volume) {
				double mvs = minfo.min_volume/bt.price.price;
				minsize = std::max(minsize, mvs);
			}
			if (order.size && std::abs(order.size) < minsize) {
				if (std::abs(order.size)<minsize*0.5) {
					order.size = 0;
					enable_alert = false;
				} else {
					order.size = sgn(order.size)*minsize;
				}
			}
			if (cfg.max_size && std::abs(order.size) > cfg.max_size) {
				order.size = cfg.max_size*sgn(order.size);
			}

			if (cfg.trade_within_budget && order.size * pos > 0 && s.calcCurrencyAllocation(order.size)<0) {
				order.size = 0;
				bt.event = BTEvent::no_balance;
			}


			if (!minfo.leverage) {
				if (order.size+pos < 0) {
					order.size = -pos;
				}
				double chg = order.size*p;
				if (balance - chg < 0 || pos + order.size < -(std::abs(pos) + std::abs(order.size))*1e-10) {
					if (neg_bal) {
						bt.event = BTEvent::no_balance;
					} else {
						if (cfg.accept_loss) {
							s.reset();
							s.onIdle(minfo, tk, pos, adjbal);
							bt.event = BTEvent::accept_loss;
						}
						order.size = 0;
						chg = 0;
					}
				}
				balance -= chg;
				pos = pos+order.size;
			} else {
				if (balance <= 0 && prev_bal > 0) {
					bt.event = BTEvent::liquidation;
					order.size -= pos;
				} else {
					if (balance <= 0) {
						bt.event = BTEvent::no_balance;
					}
					else {
						double mb = balance + dprice * (pos + order.size);
						if (mb < 0) {
							bt.event = BTEvent::margin_call;
						}
					}
				}
				if (order.size == 0 && cfg.max_leverage && cfg.reduce_on_leverage) {
					double maxPos = adjbal/p;
					if (maxPos < std::abs(pos)) {
						maxPos = maxPos*sgn(pos);
						double diff = maxPos - pos;
						order.alert = IStrategy::Alert::disabled;
						order.price = p;
						order.size = diff;
					}
				}
				pos += order.size;
			}

			if (enable_alert) {
				auto tres = s.onTrade(minfo, p, order.size, pos, balance);
				bt.neutral_price = tres.neutralPrice;
				double norm_accum = std::isfinite(tres.normAccum)?tres.normAccum:0;
				bt.norm_accum += norm_accum;
				bt.norm_profit += std::isfinite(tres.normProfit)?tres.normProfit:0;
				bt.open_price = tres.openPrice;
				if (order.size*(order.size-norm_accum)>1) order.size -= norm_accum;
			}
			if (spend) {
				double alloc = s.calcCurrencyAllocation(p);
				if (alloc>0 && alloc<balance) {
					total_spend += balance-alloc;
					balance = alloc;
				}
			}

			bt.size = order.size;
			bt.price.price = p;
			bt.price.time = price->time;
			bt.pl = pl;
			bt.pos = pos;
			bt.bal = balance+total_spend;
			bt.unspend_balance= balance;
			bt.norm_profit_total = bt.norm_profit + bt.norm_accum * p;
			bt.info = s.dumpStatePretty(minfo);



			trades.push_back(bt);
		}

	} catch (std::exception &e) {
		if (trades.empty()) throw;
		else {
			BTTrade bt = trades.back();
			bt.price.time+=3600*1000;
			bt.info = json::Object({{"error", e.what()}});
		}
	}

	if (minfo.invert_price) {
		for (auto &&x: trades) {
			x.neutral_price = 1.0/x.neutral_price;
			x.open_price = 1.0/x.open_price;
			x.pos = -x.pos;
			x.price.price = 1.0/x.price.price;
			x.size = -x.size;
		}
	}

	return trades;
}
