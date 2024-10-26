#!/bin/bash
echo "Compiling"
g++ strat.cpp -std=c++11 -lcpprest -lcrypto -lssl
echo "Finished"
exit 0
