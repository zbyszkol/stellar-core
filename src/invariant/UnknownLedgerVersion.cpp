// Copyright 2017 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "UnknownLedgerVersion.h"
#include "ledger/LedgerDelta.h"
#include "main/Config.h"
#include "util/Logging.h"

namespace stellar
{
UnknownLedgerVersion::UnknownLedgerVersion(Config const& config)
    : mConfig{config}
{
}
UnknownLedgerVersion::~UnknownLedgerVersion() = default;

std::string
UnknownLedgerVersion::getName() const
{
    return "unknown version of the ledger";
}

std::string
UnknownLedgerVersion::check(LedgerDelta const& delta) const
{
    auto const receivedLedgerVersion = delta.getHeader().ledgerVersion;
    auto const thisLedgerVersion = mConfig.CURRENT_LEDGER_PROTOCOL_VERSION;
    if (receivedLedgerVersion > thisLedgerVersion)
    {
        CLOG(INFO, "Invariant")
            << "LEDGER_PROTOCOL_VERSION is higher than expected: "
            << "expected was equal or smaller than " << thisLedgerVersion
            << ", received " << receivedLedgerVersion;
    }

    return {};
}
}
