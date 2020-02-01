#ifndef SCRIPTTOKEN_H
#define SCRIPTTOKEN_H

#include "BTCConstants.h"
#include "Data.h"

typedef struct ScriptToken {
    enum OP op;

    // Used for OP_PUSHDATA*
    Data data;

    // Used for OP_MULTISIG*
    int neededSigs;
    Datas pubKeys;

    String error;
} ScriptToken;

Data ScriptTokenCopy(Data scriptTokenAsData);

typedef Datas ScriptTokens;

ScriptToken ScriptTokenI(ScriptTokens scriptTokens, int index);

String ScriptTokenDescription(ScriptToken scriptToken);

#endif
