// Copyright (c) 2012-2013 The PPCoin developers
// Copyright (c) 2015-2019 The PIVX developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.


//------------------------------------------------------------------------------//
// This kernel is meant for usage with PIVX 3.x codebases.                      //
// The outgoing tx is built with CreateTxIn() from each stakeinput              //
// the loop feeds it, no more of that                                           //
// extensive BOOST usage, but it still relays CTransactions.                    //
// MutableTXes are also used for basically everything.                          //
//------------------------------------------------------------------------------//

bool Stake(CStakeInput* stakeInput, unsigned int nBits, unsigned int nTimeBlockFrom, unsigned int& nTimeTx, uint256& hashProofOfStake)
{
    if(Params().NetworkID() != CBaseChainParams::REGTEST) {
        if (nTimeTx < nTimeBlockFrom)
            return error("%s : nTime violation", __func__);

        if (nTimeBlockFrom + Params().StakeMinAge() > nTimeTx) // Min age requirement
            return error("%s : min age violation - nTimeBlockFrom=%d nStakeMinAge=%d nTimeTx=%d",
                         __func__, nTimeBlockFrom, Params().StakeMinAge(), nTimeTx);
    }

    //grab difficulty
    uint256 bnTargetPerCoinDay;
    bnTargetPerCoinDay.SetCompact(nBits);

    //grab stake modifier
    uint64_t nStakeModifier = 0;
    if (!stakeInput->GetModifier(nStakeModifier))
        return error("%s : failed to get kernel stake modifier", __func__);

    bool fSuccess = false;
    unsigned int nTryTime = 0;
    int nHeightStart = chainActive.Height();
    int GrindWindow = 1209600; // Two weeks by default. The lower, the more you'll need to grind. The higher, the more you'll need to wait.
    CDataStream ssUniqueID = stakeInput->GetUniqueness();
    CAmount nValueIn = stakeInput->GetValue();
    for (int i = 0; i < nHashDrift; i++) //iterate the hashing
    {
        //new block came in, move on
        if (chainActive.Height() != nHeightStart)
            break;

        //hash this iteration
        nTryTime = nTimeTx + nHashDrift - i;

        // if stake hash does not meet the target then continue to next iteration
        if (!CheckStake(ssUniqueID, nValueIn, nStakeModifier, bnTargetPerCoinDay, nTimeBlockFrom, nTryTime, hashProofOfStake)) {
            /* Let's stop at the last iteration of this loop. 
             * If this happens and we still didn't get a stake, 
             * we have a stale input. Let's grind it.
             */
            if (i == (nHashDrift - 1) && fSuccess == false) {
                
                LogPrintf("I think a tx won't hit in the current drift frame (%s), i'll re-send it and we can try our luck again\n", DateTimeStrFormat("%Y-%m-%d %H:%M:%S", nTryTime + nHashDrift + 1).c_str());
                
                // Start by making a tx
                CMutableTransaction rawTx;
                bool fBroadcast = true; //Control broadcasting behaviour
                
                // Choose a vin, using CreateTxIn() from the current (stale) stakeInput
                uint256 hashTxOut = rawTx.GetHash();
                CTxIn in;
                stakeInput->CreateTxIn(pwalletMain, in, hashTxOut);
                rawTx.vin.emplace_back(in);

                // Make a vout to an address of your choice
                CBitcoinAddress address("");
                CScript scriptPubKey = GetScriptForDestination(address.Get());
                // Choose your fee / try free txes if you want, currently set to "free/0-fee".
                CAmount nAmount = nValueIn;
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

                    for (const CTxIn& txin : rawTx.vin) {
                        const uint256& prevHash = txin.prevout.hash;
                        CCoins coins;
                        view.AccessCoins(prevHash);
                    }

                    view.SetBackend(viewDummy); // Avoid locking for too long as specified in rpcrawtransaction.cpp
                }
                
                // Grab some keys
                bool fGivenKeys = false; // Set to false if you want to choose your own keys (use tempKeystore or equivalent for that)
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
                    
                    // Sign the corresponding output:
                    if (!fHashSingle || (i < rawTx.vout.size())) {
                        SignSignature(keystore, prevPubKey, rawTx, i, nHashType);
                    }
                    
                    // Make sure we verify the tx
                    if (!VerifyScript(txin.scriptSig, prevPubKey, STANDARD_SCRIPT_VERIFY_FLAGS, MutableTransactionSignatureChecker(&rawTx, i))){
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
                        if (!AcceptToMemoryPool(mempool, state, tx, false, NULL, !fOverrideFees)) {
                            if (state.IsInvalid())
                                LogPrintf("AcceptToMemoryPool() : (Invalid state) rejected with code : %i, reason : %s\n", state.GetRejectCode(), state.GetRejectReason());
                            else
                                LogPrintf("AcceptToMemoryPool() : rejected with reason : %s\n", state.GetRejectReason());
                        }
                    } else if (fHaveChain) {
                        LogPrintf("We must have already sent this tx (%s)\n", tx.GetHash().ToString());
                    }
                LogPrintf("Ok, built a new tx (%s) for %s, i'll relay it and we can try our luck later with it\n", tx.GetHash().ToString(), in.ToString());
                RelayTransaction(tx);
                }
            }
            continue;
        }

        fSuccess = true; // if we make it this far then we have successfully created a stake hash
        //LogPrintf("%s : hashproof=%s\n", __func__, hashProofOfStake.GetHex());
        nTimeTx = nTryTime;
        continue; // This loop continues forever. It'll also take care of diff adjustments and takes around 30 blocks to be sure about an UTXO's grinding ability
    }

    // We don't need to keep track of anything, and also will return false to retain compatibility with CreateCoinstake()
    return false;
}