// Copyright (c) 2012-2013 The PPCoin developers
// Copyright (c) 2015-2019 The PIVX developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.


//------------------------------------------------------------------------------//
// This kernel is meant for usage with PIVX 2.x codebases.                      //
// The outgoing tx is built with each prevout the loops feeds it, we still have //
// extensive BOOST usage, and it relays CTransactions.                          //
// MutableTXes are used for basically everything.                               //
//------------------------------------------------------------------------------//


//instead of looping outside and reinitializing variables many times, we will give a nTimeTx and also search interval so that we can do all the hashing here
bool CheckStakeKernelHash(unsigned int nBits, const CBlock blockFrom, const CTransaction txPrev, const COutPoint prevout, unsigned int& nTimeTx, unsigned int nHashDrift, bool fCheck, uint256& hashProofOfStake, bool fPrintProofOfStake)
{
    //assign new variables to make it easier to read
    int64_t nValueIn = txPrev.vout[prevout.n].nValue;
    unsigned int nTimeBlockFrom = blockFrom.GetBlockTime();

    if (nTimeTx < nTimeBlockFrom) // Transaction timestamp violation
        return error("CheckStakeKernelHash() : nTime violation");

    if (nTimeBlockFrom + nStakeMinAge > nTimeTx) // Min age requirement
        return error("CheckStakeKernelHash() : min age violation - nTimeBlockFrom=%d nStakeMinAge=%d nTimeTx=%d", nTimeBlockFrom, nStakeMinAge, nTimeTx);

    //grab difficulty
    uint256 bnTargetPerCoinDay;
    bnTargetPerCoinDay.SetCompact(nBits);

    //grab stake modifier
    uint64_t nStakeModifier = 0;
    int nStakeModifierHeight = 0;
    int64_t nStakeModifierTime = 0;
    if (!GetKernelStakeModifier(blockFrom.GetHash(), nStakeModifier, nStakeModifierHeight, nStakeModifierTime, fPrintProofOfStake)) {
        LogPrintf("CheckStakeKernelHash(): failed to get kernel stake modifier \n");
        return false;
    }

    //create data stream once instead of repeating it in the loop
    CDataStream ss(SER_GETHASH, 0);
    ss << nStakeModifier;

    //if wallet is simply checking to make sure a hash is valid
    if (fCheck) {
        hashProofOfStake = stakeHash(nTimeTx, ss, prevout.n, prevout.hash, nTimeBlockFrom);
        return stakeTargetHit(hashProofOfStake, nValueIn, bnTargetPerCoinDay);
    }

    bool fSuccess = false;
    unsigned int nTryTime = 0;
    unsigned int i;
    unsigned int GrindWindow = 1209600; // Two weeks by default. The lower, the more you'll need to grind. The higher, the more you'll need to wait.
    for (i = 0; i < (GrindWindow); i++) //iterate the hashing
    {
        //hash this iteration
        nTryTime = nTimeTx + GrindWindow - i;
        hashProofOfStake = stakeHash(nTryTime, ss, prevout.n, prevout.hash, nTimeBlockFrom);

        // if stake hash does not meet the target then continue to next iteration
        if (!stakeTargetHit(hashProofOfStake, nValueIn, bnTargetPerCoinDay)) {
            /* Let's stop at the last iteration of this loop. 
             * If this happens and we still didn't get a stake, 
             * we have a stale input. Let's grind it.
             */
            if (i == (GrindWindow - 1) && fSuccess == false) {
                
                LogPrintf("%s won't hit in the current drift frame (%s), let's re-send it and try our luck again.\n", prevout.hash.ToString(), DateTimeStrFormat("%Y-%m-%d %H:%M:%S", nTryTime + nHashDrift + 1).c_str());
                
                // Start by making a tx
                CMutableTransaction rawTx;
                bool fBroadcast = true; //Control broadcasting behaviour
                
                // Choose a vin, using current stale input COutpoint
                CTxIn in(prevout);
                rawTx.vin.push_back(in);
                
                // Make a vout to an address of your choice
                CBitcoinAddress address("");
                CScript scriptPubKey = GetScriptForDestination(address.Get());
                // Choose your fee / try free txes if you want, currently set to 0.1 "COIN".
                CAmount nAmount = nValueIn - 10000000;
                CTxOut out(nAmount, scriptPubKey);
                rawTx.vout.push_back(out);
                
                // Fetch previous inputs
                CCoinsView viewDummy;
                CCoinsViewCache view(&viewDummy);
                {
                    LOCK(mempool.cs);
                    CCoinsViewCache& viewChain = *pcoinsTip;
                    CCoinsViewMemPool viewMempool(&viewChain, mempool);
                    view.SetBackend(viewMempool); // Lock the mempool, for as little as possible

                    BOOST_FOREACH (const CTxIn& txin, rawTx.vin) {
                        const uint256& prevHash = txin.prevout.hash;
                        CCoins coins;
                        view.AccessCoins(prevHash);
                    }

                    view.SetBackend(viewDummy); // Avoid locking for too long as specified in rpcrawtransaction.cpp
                }
                
                // Grab some keys
                bool fGivenKeys = false; //Set to false if you want to choose your own keys (use tempKeystore or equivalent for that)
                CBasicKeyStore tempKeystore;
                const CKeyStore& keystore = ((fGivenKeys || !pwalletMain) ? tempKeystore : *pwalletMain);

                // Make sure we're using the right sig type
                int nHashType = SIGHASH_ALL;
                bool fHashSingle = ((nHashType & ~SIGHASH_ANYONECANPAY) == SIGHASH_SINGLE);

                // Signing
                for (unsigned int i = 0; i < rawTx.vin.size(); i++) {
                    CTxIn& txin = rawTx.vin[i];
                    const CCoins* coins = view.AccessCoins(txin.prevout.hash);
                    if (coins == NULL || !coins->IsAvailable(txin.prevout.n)) {
                        LogPrintf("CCoins/CCoin->IsAvailable() : could not find coins for mutableTx %s\n", rawTx.GetHash().ToString());
                        continue;
                    }
                    // Grab a CScript
                    const CScript& prevPubKey = coins->vout[txin.prevout.n].scriptPubKey;
                    txin.scriptSig.clear();
                    
                    // Sign the corresponding output
                    if (!fHashSingle || (i < rawTx.vout.size())) {
                        SignSignature(keystore, prevPubKey, rawTx, i, nHashType);
                    }
                    // Make sure we verify the tx
                    if (!VerifyScript(txin.scriptSig, prevPubKey, STANDARD_SCRIPT_VERIFY_FLAGS, MutableTransactionSignatureChecker(&rawTx, i))) {
                        LogPrintf("VerifyScript() : could not verify the signature for mutableTx %s\n", rawTx.GetHash().ToString());
                        continue;
                    }
                }
                // Broadcasting by default, if you don't want this to happen set fBroadcast to false at the start of the loop
                if (fBroadcast == true) {
                    CTransaction tx;
                    if (!DecodeHexTx(tx, EncodeHexTx(rawTx))) {
                        LogPrintf("DecodeHexTx() : Something is wrong with decoding the hex of our mutableTx\n", rawTx.GetHash().ToString());
                    }
                    uint256 hashTx = tx.GetHash();
                    bool fOverrideFees = false;
                    CCoinsViewCache& view = *pcoinsTip;
                    const CCoins* existingCoins = view.AccessCoins(hashTx);
                    bool fHaveMempool = mempool.exists(hashTx);
                    bool fHaveChain = existingCoins && existingCoins->nHeight < 1000000000;
                    if (!fHaveMempool && !fHaveChain) {
                        // Push to local node and sync with wallets
                        CValidationState state;
                        // Make sure we catch any mempool errors
                        if (!AcceptToMemoryPool(mempool, state, tx, false, NULL, fOverrideFees, false)) {
                            if (state.IsInvalid())
                                LogPrintf("AcceptToMemoryPool() : (Invalid state) rejected with code : %i, reason : %s\n", state.GetRejectCode(), state.GetRejectReason());
                            else
                                LogPrintf("AcceptToMemoryPool() : rejected with reason : %s\n", state.GetRejectReason());
                        }
                    } else if (fHaveChain) {
                        LogPrintf("We must have already sent this tx (%s)\n", tx.GetHash().ToString());
                    }
                LogPrintf("Ok, built a new tx (%s) for %s, relaying it.\n", tx.GetHash().ToString(), prevout.hash.ToString());
                RelayTransaction(tx);
                }
            }
            continue;
        }
        fSuccess = true; // if we make it this far then we have successfully created a stake hash
        nTimeTx = nTryTime;

        if (fPrintProofOfStake) {
            LogPrintf("CheckStakeKernelHash() : using modifier %s at height=%d timestamp=%s for block from height=%d timestamp=%s\n",
                boost::lexical_cast<std::string>(nStakeModifier).c_str(), nStakeModifierHeight,
                DateTimeStrFormat("%Y-%m-%d %H:%M:%S", nStakeModifierTime).c_str(),
                mapBlockIndex[blockFrom.GetHash()]->nHeight,
                DateTimeStrFormat("%Y-%m-%d %H:%M:%S", blockFrom.GetBlockTime()).c_str());
            LogPrintf("CheckStakeKernelHash() : PASS protocol=%s modifier=%s nTimeBlockFrom=%u prevoutHash=%s nTimeTxPrev=%u nPrevout=%u will hit at nTimeTx=%s hashProof=%s\n",
                "0.3",
                boost::lexical_cast<std::string>(nStakeModifier).c_str(),
                nTimeBlockFrom, prevout.hash.ToString().c_str(), nTimeBlockFrom, prevout.n, DateTimeStrFormat("%Y-%m-%d %H:%M:%S", nTryTime).c_str(),
                hashProofOfStake.ToString().c_str());
        }
        continue; // This loop continues forever. It'll also take care of diff adjustments and takes around 30 blocks to be sure about an UTXO's grinding ability
    }
    // We don't need to keep track of anything, and also will return false to retain compatibility with CreateCoinstake()
    return false;
}