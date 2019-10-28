# Small collection of stake-grinding kernels for most PoS 3 based cryptocurrencies.

##### This repository contains a few lightly modified "grinding" kernels files for usage in different cryptocurrency source code, mostly different PIVX forks and their derivatives. 

The code will iterate each UTXO in the GrindWindow timeframe (2 weeks by default), send stale inputs back to a user-specified address and proceed printing out each successful grind and when that input is expected to mint successfully.

Keeping it running will make sure the input stays successful, as difficulty can have some effect on staking probabilities. Choosing a too long GrindWindow increases difficulty in grinding.

I found this very useful when studying the PoS protocol, i will update this repo from time to time with different kernel types (This currently covers **kernel 3**, I plan on adding support for **kernel 4** and **kernel 5** soon enough).

##### Live Demos:

[COLX](https://chainz.cryptoid.info/colx/address.dws?DJB3pXt9Xuz7UTwPg4R8YtXB75gpNmkErD.htm)(PIVX fork, old PoS 3 kernel, uses the 2.x kernel from this repo)

As of 28/10/2019, wins an average of 2% of each 1000 block round, with a 0.023% "real" stake (as a comparison, the best staking address at the moment wins 3.3% of the same round on average, with about 2.06% of the average stake pool). This means we have amplified our stake almost 100 times; very bad.

[PIVX](https://chainz.cryptoid.info/pivx/address.dws?DRULZUb5AX4srKsmQ9B8PK8SJmezG9uyub.htm)(The usual suspect, new kernel, old problems)

As of 28/10/2019, address wins all the MN reward stakes repeatedly. Didn't run the numbers down on this bad boy as i just started analysing it yesterday. Definetely something worth looking into for the new kernel pivx came up with. I guess it wasn't such a great idea to use masternodes for this. (But then again, when ever is it a good idea)


Reading comments can help with any further question. If you want to contribute, open a pull request against this repository, or contact me on Discord or Twitter _@giaki3003_ or by email _giaki3003@gmail.com_.