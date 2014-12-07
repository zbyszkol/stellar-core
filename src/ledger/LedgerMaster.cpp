#include "LedgerMaster.h"
#include "main/Application.h"

/*
The ledger module:
    1) gets the externalized tx set
    2) applies this set to the previous ledger
    3) sends the changed entries to the CLF
    4) saves the changed entries to SQL
    5) saves the ledger hash and header to SQL
    6) sends the new ledger hash and the tx set to the history
    7) sends the new ledger hash and header to the TxHerder
    

catching up to network:
    1) Wait for FBA to tell us what the network is on on now
    2) Ask network for the  

    // TODO.1 need to make sure the CLF and the SQL Ledger are in sync on start up
    // TODO.1 need to also come to consensus on the base fee

*/
namespace stellar
{
    LedgerMaster::LedgerMaster() 
	{
		mCaughtUp = false;
        reset();
	}

    // make sure our state is consistent with the CLF
    void LedgerMaster::loadLastKnownCLF()
    {
        LedgerHeaderPtr clfHeader=mApp->getCLFGateway().getCurrentHeader();


        bool needreset = true;
        stellarxdr::uint256 lkcl = getLastClosedLedgerHash();
        if(lkcl.isNonZero()) {
            // there is a ledger in the database
            if(mCurrentCLF->load(lkcl)) {
                mLastLedgerHash = lkcl;
                needreset = false;
            }
        }
        if(needreset) {
            reset();
        }
    }

    // called by txherder
    void LedgerMaster::externalizeValue(BallotPtr balllot, TransactionSet::pointer txSet)
    {
        if(mLastLedgerHash == balllot->mPreviousLedgerHash)
            closeLedger(txSet);
        else
        { // we need to catch up
            if(mApp->mState == Application::CATCHING_UP_STATE)
            {  // we are already trying to catch up

            } else
            {  // start trying to catchup
                startCatchUp()
            }
           
        }
    }

    // we have some last ledger that is in the DB
    // we need to 
    void LedgerMaster::startCatchUp(BallotPtr balllot)
    {
        mApp->mState = Application::CATCHING_UP_STATE;

    }

    // called by CLF
    void LedgerMaster::ledgerHashComputed(stellarxdr::uint256& hash)
    {
        // NICOLAS
    }

    void LedgerMaster::reset()
    {
        // NICOLAS mCurrentCLF = std::make_shared<LegacyCLF>(); // change this to BucketList when we are ready
        mLastLedgerHash = stellarxdr::uint256();
    }

    void LedgerMaster::closeLedger(TransactionSet::pointer txSet)
    {
        for(auto tx : txSet->mTransactions)
        {

        }
    }


    /* NICOLAS

	Ledger::pointer LedgerMaster::getCurrentLedger()
	{
		return(Ledger::pointer());
	}

    bool LedgerMaster::ensureSync(ripple::Ledger::pointer lastClosedLedger)
    {
        bool res = false;
        // first, make sure we're in sync with the world
        if (lastClosedLedger->getHash() != mLastLedgerHash)
        {
            std::vector<stellarxdr::uint256> needed=lastClosedLedger->getNeededAccountStateHashes(1,NULL);
            if(needed.size())
            {
                // we're missing some nodes
                return false;
            }

            try
            {
                CanonicalLedgerForm::pointer newCLF, currentCLF = std::make_shared<LegacyCLF>(lastClosedLedger));
                mCurrentDB.beginTransaction();
                try
                {
                    newCLF = catchUp(currentCLF);
                }
                catch (...)
                {
                    mCurrentDB.endTransaction(true);
                }

                if (newCLF)
                {
                    mCurrentDB.endTransaction(false);
                    setLastClosedLedger(newCLF);
                    res = true;
                }
            }
            catch (...)
            {
                // problem applying to the database
                CLOG(ripple::ERROR, ripple::Ledger) << "database error";
            }
        }
        else
        { // already tracking proper ledger
            res = true;
        }

        return res;
    }

    void LedgerMaster::beginClosingLedger()
    {
        // ready to make changes
        mCurrentDB.beginTransaction();
        assert(mCurrentDB.getTransactionLevel() == 1); // should be top level transaction
    }

	bool  LedgerMaster::commitLedgerClose(ripple::Ledger::pointer ledger)
	{
        bool res = false;
        CanonicalLedgerForm::pointer newCLF;

        assert(ledger->getParentHash() == mLastLedgerHash); // should not happen

        try
        {
            CanonicalLedgerForm::pointer nl = std::make_shared<LegacyCLF>(ledger);
            try
            {
                // only need to update ledger related fields as the account state is already in SQL
                updateDBFromLedger(nl);
                newCLF = nl;
            }
            catch (std::runtime_error const &)
            {
                CLOG(ripple::ERROR, ripple::Ledger) << "Ledger close: could not update database";
            }

            if (newCLF != nullptr)
            {
                mCurrentDB.endTransaction(false);
                setLastClosedLedger(newCLF);
                res = true;
            }
            else
            {
                mCurrentDB.endTransaction(true);
            }
        }
        catch (...)
        {
        }
        return res;
	}

    void LedgerMaster::setLastClosedLedger(CanonicalLedgerForm::pointer ledger)
    {
        // should only be done outside of transactions, to guarantee state reflects what is on disk
        assert(mCurrentDB.getTransactionLevel() == 0);
        mCurrentCLF = ledger;
        mLastLedgerHash = ledger->getHash();
        CLOG(ripple::lsINFO, ripple::Ledger) << "Store at " << mLastLedgerHash;
    }

    void LedgerMaster::abortLedgerClose()
    {
        mCurrentDB.endTransaction(true);
    }

	

	CanonicalLedgerForm::pointer LedgerMaster::catchUp(CanonicalLedgerForm::pointer updatedCurrentCLF)
	{
		// new SLE , old SLE
		SHAMap::Delta delta;
        bool needFull = false;

        CLOG(ripple::lsINFO, ripple::Ledger) << "catching up from " << mCurrentCLF->getHash() << " to " << updatedCurrentCLF;

        try
        {
            if (mCurrentCLF->getHash().isZero())
            {
                needFull = true;
            }
            else
            {
		        updatedCurrentCLF->getDeltaSince(mCurrentCLF,delta);
            }
        }
        catch (std::runtime_error const &e)
        {
            CLOG(ripple::WARNING, ripple::Ledger) << "Could not compute delta: " << e.what();
            needFull = true;
        };

        if (needFull){
            return importLedgerState(updatedCurrentCLF->getHash());
        }

        // incremental update

        mCurrentDB.beginTransaction();

        try {
            BOOST_FOREACH(SHAMap::Delta::value_type it, delta)
		    {
                SLE::pointer newEntry = updatedCurrentCLF->getLegacyLedger()->getSLEi(it.first);
                SLE::pointer oldEntry = mCurrentCLF->getLegacyLedger()->getSLEi(it.first);

			    if(newEntry)
			    {
				    LedgerEntry::pointer entry = LedgerEntry::makeEntry(newEntry);
				    if(oldEntry)
				    {	// SLE updated
					    if(entry) entry->storeChange();
				    } else
				    {	// SLE added
					    if(entry) entry->storeAdd();
				    }
			    } else
			    { // SLE must have been deleted
                    assert(oldEntry);
				    LedgerEntry::pointer entry = LedgerEntry::makeEntry(oldEntry);
				    if(entry) entry->storeDelete();
			    }			
		    }
            updateDBFromLedger(updatedCurrentCLF);
        }
        catch (...) {
            mCurrentDB.endTransaction(true);
            throw;
        }

        mCurrentDB.endTransaction(false);

        return updatedCurrentCLF;
	}


    static void importHelper(SLE::ref curEntry, LedgerMaster &lm) {
        LedgerEntry::pointer entry = LedgerEntry::makeEntry(curEntry);
        if(entry) {
            entry->storeAdd();
        }
        // else entry type we don't care about
    }
    
    CanonicalLedgerForm::pointer LedgerMaster::importLedgerState(stellarxdr::uint256 ledgerHash)
    {
        CanonicalLedgerForm::pointer res;

        CLOG(ripple::lsINFO, ripple::Ledger) << "Importing full ledger " << ledgerHash;

        CanonicalLedgerForm::pointer newLedger = std::make_shared<LegacyCLF>();

        if (newLedger->load(ledgerHash)) {
            mCurrentDB.beginTransaction();
            try {
                // delete all
                LedgerEntry::dropAll(mCurrentDB);

                // import all anew
                newLedger->getLegacyLedger()->visitStateItems(BIND_TYPE (&importHelper, P_1, boost::ref (*this)));

                updateDBFromLedger(newLedger);
            }
            catch (...) {
                mCurrentDB.endTransaction(true);
                CLOG(ripple::WARNING, ripple::Ledger) << "Could not import state";
                return CanonicalLedgerForm::pointer();
            }
            mCurrentDB.endTransaction(false);
            res = newLedger;
        }
        return res;
    }

    void LedgerMaster::updateDBFromLedger(CanonicalLedgerForm::pointer ledger)
    {
        stellarxdr::uint256 currentHash = ledger->getHash();
        string hex(to_string(currentHash));

        mCurrentDB.setState(mCurrentDB.getStoreStateName(LedgerDatabase::kLastClosedLedger), hex.c_str());
    }

    stellarxdr::uint256 LedgerMaster::getLastClosedLedgerHash()
    {
        string h = mCurrentDB.getState(mCurrentDB.getStoreStateName(LedgerDatabase::kLastClosedLedger));
        return stellarxdr::uint256(h); // empty string -> 0
    }

    void LedgerMaster::closeLedger(TransactionSet::pointer txSet)
	{
        mCurrentDB.beginTransaction();
        assert(mCurrentDB.getTransactionLevel() == 1);
        try {
		// apply tx set to the last ledger
            // todo: needs the logic to deal with partial failure
		    for(int n = 0; n < txSet->mTransactions.size(); n++)
		    {
			    txSet->mTransactions[n].apply();
		    }

		    // save collected changes to the bucket list
		    mCurrentCLF->closeLedger();

		    // save set to the history
		    txSet->store();
        }
        catch (...)
        {
            mCurrentDB.endTransaction(true);
            throw;
        }
        mCurrentDB.endTransaction(false);
        // NICOLAS this code is incomplete
	}
    */

}