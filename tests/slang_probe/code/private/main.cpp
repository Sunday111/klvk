// Proves the Slang binary dependency is fetched, linked, includable, and its
// shared library resolvable at run time. Creating a global session touches all
// four: it is the entry point every Slang compilation goes through, so if this
// returns cleanly the compiler is genuinely usable from a yae build.

#include <slang-com-ptr.h>
#include <slang.h>

#include <cstdio>

int main()
{
    Slang::ComPtr<slang::IGlobalSession> global_session;
    const SlangResult result = slang::createGlobalSession(global_session.writeRef());
    if (SLANG_FAILED(result) || !global_session)
    {
        std::fprintf(stderr, "slang: failed to create global session (0x%08x)\n", static_cast<unsigned>(result));
        return 1;
    }

    std::printf("slang global session created; build tag: %s\n", global_session->getBuildTagString());
    return 0;
}
