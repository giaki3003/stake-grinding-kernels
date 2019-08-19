# Small collection of stake-grinding kernels for most PoS 3 based cryptocurrencies.

##### This repository contains a few lightly modified "grinding" kernels files for usage in different cryptocurrency source code, mostly different PIVX forks and their derivatives. 

The code will iterate each UTXO in the GrindWindow timeframe (2 weeks by default), send stale inputs back to a user-specified address and proceed printing out each successful grind and when that input is expected to mint successfully.

Keeping it running will make sure the input stays successful, as difficulty can have some effect on staking probabilities. Choosing a too long GrindWindow increases difficulty in grinding.

I found this very useful when studying the PoS protocol, i will update this repo from time to time with different kernel types (This currently covers **kernel 3**, I plan on adding support for **kernel 4** and **kernel 5** soon enough).

Reading comments can help with any further question. If you want to contribute, open a pull request against this repository, or contact me on Discord or Twitter _@giaki3003_ or by email _giaki3003@gmail.com_.