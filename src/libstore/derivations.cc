#include "derivations.hh"
#include "store-api.hh"
#include "aterm.hh"
#include "globals.hh"

#include "derivations-ast.hh"
#include "derivations-ast.cc"


namespace nix {


Hash hashTerm(ATerm t)
{
    return hashString(htSHA256, atPrint(t));
}


Path writeDerivation(const Derivation & drv, const string & name)
{
    PathSet references;
    references.insert(drv.inputSrcs.begin(), drv.inputSrcs.end());
    for (DerivationInputs::const_iterator i = drv.inputDrvs.begin();
         i != drv.inputDrvs.end(); ++i)
        references.insert(i->first);
    /* Note that the outputs of a derivation are *not* references
       (that can be missing (of course) and should not necessarily be
       held during a garbage collection). */
    string suffix = name + drvExtension;
    string contents = atPrint(unparseDerivation(drv));
    return readOnlyMode
        ? computeStorePathForText(suffix, contents, references)
        : store->addTextToStore(suffix, contents, references);
}


static void checkPath(const string & s)
{
    if (s.size() == 0 || s[0] != '/')
        throw Error(format("bad path `%1%' in derivation") % s);
}
    

static void parseStrings(ATermList paths, StringSet & out, bool arePaths)
{
    for (ATermIterator i(paths); i; ++i) {
        if (ATgetType(*i) != AT_APPL)
            throw badTerm("not a path", *i);
        string s = aterm2String(*i);
        if (arePaths) checkPath(s);
        out.insert(s);
    }
}


/* Shut up warnings. */
void throwBadDrv(ATerm t) __attribute__ ((noreturn));

void throwBadDrv(ATerm t) 
{
    throw badTerm("not a valid derivation", t);
}


Derivation parseDerivation(ATerm t)
{
    Derivation drv;
    ATermList outs, stateOuts, stateOutDirs, inDrvs, inSrcs, args, bnds;
    ATerm builder, platform;

    if (!matchDerive(t, outs, stateOuts, stateOutDirs, inDrvs, inSrcs, platform, builder, args, bnds))
        throwBadDrv(t);

    for (ATermIterator i(outs); i; ++i) {
        ATerm id, path, hashAlgo, hash;
        if (!matchDerivationOutput(*i, id, path, hashAlgo, hash))
            throwBadDrv(t);
        DerivationOutput out;
        out.path = aterm2String(path);
        checkPath(out.path);
        out.hashAlgo = aterm2String(hashAlgo);
        out.hash = aterm2String(hash);
        drv.outputs[aterm2String(id)] = out;
    }
    
    //parse state part
    for (ATermIterator i(stateOuts); i; ++i) {
        ATerm id, statepath, hashAlgo, hash, enabled, shared, synchronization;
        if (!matchDerivationStateOutput(*i, id, statepath, hashAlgo, hash, enabled, shared, synchronization))
            throwBadDrv(t);
        DerivationStateOutput stateOut;
        stateOut.statepath = aterm2String(statepath);
        //checkPath(stateOut.path);									//should we check the statpath .... ???
        stateOut.hashAlgo = aterm2String(hashAlgo);
        stateOut.hash = aterm2String(hash);
        stateOut.enabled = aterm2String(enabled);
        stateOut.shared = aterm2String(shared);
        stateOut.synchronization = aterm2String(synchronization);
        drv.stateOutputs[aterm2String(id)] = stateOut;
    }
    
    //parse state dirs part
    for (ATermIterator i(stateOutDirs); i; ++i) {
        ATerm id, path, type, interval;
        if (!matchDerivationStateOutputDir(*i, id, /*path,*/ type, interval))
            throwBadDrv(t);
        path = id;    
        DerivationStateOutputDir stateOutDirs;
        stateOutDirs.path = aterm2String(path);
        stateOutDirs.type = aterm2String(type);
        stateOutDirs.interval = aterm2String(interval);
        drv.stateOutputDirs[aterm2String(id)] = stateOutDirs;
    }

    for (ATermIterator i(inDrvs); i; ++i) {
        ATerm drvPath;
        ATermList ids;
        if (!matchDerivationInput(*i, drvPath, ids))
            throwBadDrv(t);
        Path drvPath2 = aterm2String(drvPath);
        checkPath(drvPath2);
        StringSet ids2;
        parseStrings(ids, ids2, false);
        drv.inputDrvs[drvPath2] = ids2;
    }
    
    parseStrings(inSrcs, drv.inputSrcs, true);

    drv.builder = aterm2String(builder);
    drv.platform = aterm2String(platform);
    
    for (ATermIterator i(args); i; ++i) {
        if (ATgetType(*i) != AT_APPL)
            throw badTerm("string expected", *i);
        drv.args.push_back(aterm2String(*i));
    }

    for (ATermIterator i(bnds); i; ++i) {
        ATerm s1, s2;
        if (!matchEnvBinding(*i, s1, s2))
            throw badTerm("tuple of strings expected", *i);
        drv.env[aterm2String(s1)] = aterm2String(s2);
    }

    return drv;
}


ATerm unparseDerivation(const Derivation & drv)
{
    ATermList outputs = ATempty;
    for (DerivationOutputs::const_reverse_iterator i = drv.outputs.rbegin();
         i != drv.outputs.rend(); ++i)
        outputs = ATinsert(outputs,
            makeDerivationOutput(
                toATerm(i->first),
                toATerm(i->second.path),
                toATerm(i->second.hashAlgo),
                toATerm(i->second.hash)));

    ATermList stateOutputs = ATempty;
    for (DerivationStateOutputs::const_reverse_iterator i = drv.stateOutputs.rbegin(); i != drv.stateOutputs.rend(); ++i)
        stateOutputs = ATinsert(stateOutputs,
            makeDerivationStateOutput(
                toATerm(i->first),
                toATerm(i->second.statepath),
                toATerm(i->second.hashAlgo),
                toATerm(i->second.hash),
                toATerm(i->second.enabled),
                toATerm(i->second.shared),
                toATerm(i->second.synchronization)
                ));
                
    ATermList stateOutputDirs = ATempty;
    for (DerivationStateOutputDirs::const_reverse_iterator i = drv.stateOutputDirs.rbegin(); i != drv.stateOutputDirs.rend(); ++i)
        stateOutputDirs = ATinsert(stateOutputDirs,
            makeDerivationStateOutputDir(
                toATerm(i->first),
                //toATerm(i->second.path),
                toATerm(i->second.type),
                toATerm(i->second.interval)
                ));
                
   //toATermList(i->second.dirs)


    ATermList inDrvs = ATempty;
    for (DerivationInputs::const_reverse_iterator i = drv.inputDrvs.rbegin();
         i != drv.inputDrvs.rend(); ++i)
        inDrvs = ATinsert(inDrvs,
            makeDerivationInput(
                toATerm(i->first),
                toATermList(i->second)));
    
    ATermList args = ATempty;
    for (Strings::const_reverse_iterator i = drv.args.rbegin();
         i != drv.args.rend(); ++i)
        args = ATinsert(args, toATerm(*i));

    ATermList env = ATempty;
    for (StringPairs::const_reverse_iterator i = drv.env.rbegin();
         i != drv.env.rend(); ++i)
        env = ATinsert(env,
            makeEnvBinding(
                toATerm(i->first),
                toATerm(i->second)));

    return makeDerive(
        outputs,
        stateOutputs,
        stateOutputDirs,
        inDrvs,
        toATermList(drv.inputSrcs),
        toATerm(drv.platform),
        toATerm(drv.builder),
        args,
        env);
}


bool isDerivation(const string & fileName)
{
    return
        fileName.size() >= drvExtension.size() &&
        string(fileName, fileName.size() - drvExtension.size()) == drvExtension;
}

 
}
