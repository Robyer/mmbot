/*
 * stats2report.h
 *
 *  Created on: 22. 8. 2019
 *      Author: ondra
 */

#ifndef SRC_MAIN_STATS2REPORT_H_
#define SRC_MAIN_STATS2REPORT_H_

#include <memory>
#include "istatsvc.h"
#include "report.h"

using CalcSpreadFn = std::function<void()>;
using CalcSpreadQueue = std::function<void(CalcSpreadFn &&) >;


class Stats2Report: public IStatSvc {
public:



	Stats2Report(
			std::string name,
			Report &rpt
			) :rpt(rpt),name(name)  {}

	virtual void reportOrders(const std::optional<IStockApi::Order> &buy,
							  const std::optional<IStockApi::Order> &sell) override {
		rpt.setOrders(name, buy, sell);
	}
	virtual void reportTrades(ondra_shared::StringView<IStockApi::TradeWithBalance> trades, const Strategy &strategy) override {
		rpt.setTrades(name,trades,strategy);
	}
	virtual void reportMisc(const MiscData &miscData) override{
		rpt.setMisc(name, miscData);
	}
	virtual void reportError(const ErrorObj &errorObj) override{
		rpt.setError(name, errorObj);
	}

	virtual void setInfo(const Info &info) override{
		rpt.setInfo(name, info);
	}
	virtual void reportPrice(double price) override{
		rpt.setPrice(name, price);
	}
	virtual std::size_t getHash() const override {
		std::hash<std::string> h;
		return h(name);
	}
	virtual void clear() override {
		rpt.clear(name);
	}

	Report &rpt;
	std::string name;


};




#endif /* SRC_MAIN_STATS2REPORT_H_ */
