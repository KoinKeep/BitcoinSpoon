#include "ScriptToken.h"
#include "BTCUtil.h"
#include <stdlib.h>
#include <stdio.h>

Data ScriptTokenCopy(Data scriptTokenAsData)
{
    ScriptToken result = *(ScriptToken*)scriptTokenAsData.bytes;

    result.data = DataCopyData(result.data);
    result.pubKeys = DatasCopy(result.pubKeys);
    result.error = DataCopyData(result.error);

    return DataCopy((void*)&result, sizeof(result));
}

ScriptToken ScriptTokenI(ScriptTokens scriptTokens, int index)
{
    return *(ScriptToken*)scriptTokens.ptr[index].bytes;
}

String ScriptTokenDescription(ScriptToken token)
{
    DataTrackPush();

    String str = StringNew("");

    if(token.op && token.op <= OP_PUSHDATA4)
        str = StringAddF(str, "PUSH(%d) [%s]", token.data.length, toHex(token.data).bytes);
    else
        str = StringAdd(str, OPtoString(token.op));

    if(token.error.bytes) {

        str = StringAdd(str, " <- ERROR [");
        str = StringAdd(str, OPtoString(token.op));
        str = StringAdd(str, "]");
    }

    return DTPop(str);
}
