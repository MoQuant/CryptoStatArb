#include <iostream>
#include <vector>
#include <string>
#include <sstream>
#include <thread>
#include <cpprest/ws_client.h>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <algorithm>
#include <math.h>
#include <cmath>

using namespace web;
using namespace web::websockets::client;
using namespace boost::property_tree;

void FillData(std::string resp, std::vector<double> & btc, std::vector<double> & eth){
    std::stringstream ss(resp);
    ptree df;
    read_json(ss, df);
    bool items = false;
    std::string ticker;
    double price = 0;

    for(ptree::const_iterator it = df.begin(); it != df.end(); ++it){
        if(items == true){
            if(it->first == "product_id"){
                ticker = it->second.get_value<std::string>();
            }
            if(it->first == "price"){
                price = atof(it->second.get_value<std::string>().c_str());
            }
        }
        if(it->first == "type"){
            if(it->second.get_value<std::string>() == "ticker"){
                items = true;
            }
        }
    }

    if(ticker == "BTC-USD"){
        btc.push_back(price);
    }
    if(ticker == "ETH-USD"){
        eth.push_back(price);
    }
}

static void Feed(std::vector<double> & btc, std::vector<double> & eth, int limit){
    std::string url = "wss://ws-feed.exchange.coinbase.com";
    std::string message = "{\"type\":\"subscribe\",\"product_ids\":[\"BTC-USD\",\"ETH-USD\"], \"channels\":[\"ticker\"]}";

    websocket_client client;
    client.connect(url).wait();

    websocket_outgoing_message outmsg;
    outmsg.set_utf8_message(message);
    client.send(outmsg);

    while(true){
        client.receive().then([](websocket_incoming_message inmsg){
            return inmsg.extract_string();
        }).then([&](std::string response){
            FillData(response, std::ref(btc), std::ref(eth));
            if(btc.size() > limit){
                btc.erase(btc.begin());
            }
            if(eth.size() > limit){
                eth.erase(eth.begin());
            }
        }).wait();
    }

    client.close().wait();
}

std::vector<double> ROR(std::vector<double> x){
    std::vector<double> r;
    for(int i = 1; i < x.size(); ++i){
        r.push_back(log(x[i]/x[i-1]));
    }
    return r;
}

double Signal(std::vector<double> x, std::vector<double> y){
    auto average = [](std::vector<double> k){
        double total = 0;
        for(auto & l : k){
            total += l;
        }
        total /= (double) k.size();
        return total;
    };

    auto stdev = [&](std::vector<double> k){
        double total = 0;
        for(int i = 0; i < k.size(); ++i){
            total += pow(k[i] - average(k), 2);
        }
        total /= ((double) k.size() - 1);
        return pow(total, 0.5);
    };
    
    double covar = 0, varz = 0;
    int n = x.size();

    for(int i = 0; i < n; ++i){
        covar += (x[i] - average(x))*(y[i] - average(y));
        varz += pow(x[i] - average(x), 2);
    }
    
    double beta = covar/varz;
    double alpha = average(y) - beta*average(x);

    std::vector<double> spread;
    for(int i = 0; i < n; ++i){
        spread.push_back(y[i] - (alpha + beta*x[i]));
    }

    double spreadMean = average(spread);
    double spreadStdev = stdev(spread);

    double ZScore = (spread[spread.size() - 1] - spreadMean)/spreadStdev;

    return ZScore;
}

double PnL(std::vector<double> p, double oldprice){
    double tx = 0.0045;
    double price = p[p.size() - 1];
    return (price*(1-tx))/(oldprice*(1+tx)) - 1.0;
}


int main()
{
    std::vector<double> btc, eth, xbtc, xeth, rx, ex;
    int limit = 60;
    int entry = 30;

    std::thread dataset(Feed, std::ref(btc), std::ref(eth), limit);

    std::string side = "neutral";
    double btcPrice = 0;
    double ethPrice = 0;

    double btcROR = 1, ethROR = 1;

    while(true){
        if(btc.size() > entry && eth.size() > entry){
            int n = btc.size();
            int m = eth.size();
            if(n > m){
                xbtc = {btc.end() - m, btc.end()};
                xeth = {eth.end() - m, eth.end()};
            } else {
                xbtc = {btc.end() - n, btc.end()};
                xeth = {eth.end() - n, eth.end()};
            }
            rx = ROR(xbtc);
            ex = ROR(xeth);

            double signal = Signal(rx, ex);

            if(signal < -1.96 && side == "short"){
                side = "neutral";
                ethROR *= (1 + PnL(xeth, ethPrice));
                std::cout << "Profit for Ethereum: " << ethROR - 1 << std::endl;
            }

            if(signal > 1.96 && side == "neutral"){
                side = "short";
                ethPrice = xeth[xeth.size() - 1];
                std::cout << "Go Long in Ethereum" << std::endl;
            }

            if(signal > 1.96 && side == "long"){
                side = "neutral";
                btcROR *= (1 + PnL(xbtc, btcPrice));
                std::cout << "Profit for Bitcoin: " << btcROR - 1 << std::endl;
            }

            if(signal < -1.96 && side == "neutral"){
                side = "long";
                btcPrice = xbtc[xbtc.size() - 1];
                std::cout << "Go Long in Bitcoin" << std::endl;
            }

            
        } else {
            std::cout << "Loading BTC: " << entry - btc.size() << "\tETH: " << entry - eth.size() << std::endl;
        }
    }

    dataset.join();

    return 0;
}