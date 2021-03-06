/*
 * Copyright (c) 2014-2015, Xerox Corporation (Xerox)and Palo Alto Research Center (PARC)
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Patent rights are not granted under this agreement. Patent rights are
 *       available under FRAND terms.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL XEROX or PARC BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
/**
 * @author Glenn Scott, Palo Alto Research Center (Xerox PARC)
 * @copyright 2014-2015, Xerox Corporation (Xerox)and Palo Alto Research Center (PARC).  All rights reserved.
 */
#include <config.h>
#include <LongBow/runtime.h>

#include <time.h>

#include <getopt.h>
#include <stdio.h>

#include "ccnxPortalServer_About.h"

#include <ccnx/api/ccnx_Portal/ccnx_Portal.h>
#include <ccnx/api/ccnx_Portal/ccnx_PortalRTA.h>

#include <parc/algol/parc_Buffer.h>
#include <parc/algol/parc_BufferComposer.h>
#include <parc/algol/parc_Memory.h>
#include <parc/security/parc_Security.h>
#include <parc/security/parc_PublicKeySigner.h>
#include <parc/security/parc_IdentityFile.h>

#include <ccnx/common/ccnx_Name.h>

extern PARCBuffer *makePayload(const CCNxName *interestName, const char *commandString);
extern int ccnServe(const PARCIdentity *identity, const CCNxName *listenName, const char *commandString);
extern void usage(void);

PARCBuffer *
makePayload(const CCNxName *interestName, const char *commandString)
{
    char *commandToExecute;

    char *nameAsString = ccnxName_ToString(interestName);
    int failure = asprintf(&commandToExecute, commandString, nameAsString);
    assertTrue(failure > -1, "Error asprintf");
    parcMemory_Deallocate((void **) &nameAsString);

    PARCBufferComposer *accumulator = parcBufferComposer_Create();

    FILE *fp = popen(commandToExecute, "r");
    if (fp != NULL) {
        unsigned char buffer[1024];

        while (feof(fp) == 0) {
            size_t length = fread(buffer, sizeof(char), sizeof(buffer), fp);
            parcBufferComposer_PutArray(accumulator, buffer, length);
        }
        pclose(fp);
    } else {
        parcBufferComposer_PutString(accumulator, "Cannot execute: ");
        parcBufferComposer_PutString(accumulator, commandString);
    }

    PARCBuffer *payload = parcBufferComposer_ProduceBuffer(accumulator);
    parcBufferComposer_Release(&accumulator);

    return payload;
}

int
ccnServe(const PARCIdentity *identity, const CCNxName *listenName, const char *commandString)
{
    parcSecurity_Init();

    CCNxPortalFactory *factory = ccnxPortalFactory_Create(identity);

    CCNxPortal *portal = ccnxPortalFactory_CreatePortal(factory, ccnxPortalRTA_Message);
    assertNotNull(portal, "Expected a non-null CCNxPortal pointer.");

    if (ccnxPortal_Listen(portal, listenName, 365 * 86400, CCNxStackTimeout_Never)) {
        while (true) {
            CCNxMetaMessage *request = ccnxPortal_Receive(portal, CCNxStackTimeout_Never);

            if (request == NULL) {
                break;
            }

            CCNxInterest *interest = ccnxMetaMessage_GetInterest(request);

            if (interest != NULL) {
                CCNxName *interestName = ccnxInterest_GetName(interest);

                PARCBuffer *payload = makePayload(interestName, commandString);

                CCNxContentObject *contentObject = ccnxContentObject_CreateWithNameAndPayload(interestName, payload);

                CCNxMetaMessage *message = ccnxMetaMessage_CreateFromContentObject(contentObject);
                if (ccnxPortal_Send(portal, message, CCNxStackTimeout_Never) == false) {
                    fprintf(stderr, "ccnxPortal_Write failed: %d\n", ccnxPortal_GetError(portal));
                }
                {
                    char *name = ccnxName_ToString(interestName);
                    time_t theTime = time(0);
                    char *time = ctime(&theTime);
                    printf("%24.24s  %s\n", time, name);
                    parcMemory_Deallocate((void **) &name);
                }

                parcBuffer_Release(&payload);
            }
            ccnxMetaMessage_Release(&request);
        }
    }

    ccnxPortal_Release(&portal);

    ccnxPortalFactory_Release(&factory);

    parcSecurity_Fini();

    return 0;
}

void
usage(void)
{
    printf("ccnx-server --identity <file> --password <password> lci:/ccn-name command-to-execute\n");
    printf("ccnx-server [-h | --help]\n");
    printf("ccnx-server [-v | --version]\n");
    printf("\n");
    printf("    --identity         The file name containing a PKCS12 keystore\n");
    printf("    --password         The password to unlock the keystore\n");
    printf("    lci:/ccn-name      The LCI name of the object fetch\n");
    printf("    program-to-execute The program to run (eg. /bin/date)\n");
}

int
main(int argc, char *argv[argc])
{
    char *keystoreFile = NULL;
    char *keystorePassword = NULL;
    char *commandString = "/bin/date";
    char *listenName = "lci:/Server";

    /* options descriptor */
    static struct option longopts[] = {
        { "identity", required_argument, NULL, 'f' },
        { "password", required_argument, NULL, 'p' },
        { "help",     no_argument,       NULL, 'h' },
        { "version",  no_argument,       NULL, 'v' },
        { NULL,       0,                 NULL, 0   }
    };

    if (argc < 2) {
        usage();
        exit(1);
    }

    int ch;
    while ((ch = getopt_long(argc, argv, "fphvc:", longopts, NULL)) != -1) {
        switch (ch) {
            case 'f':
                keystoreFile = optarg;
                break;

            case 'p':
                keystorePassword = optarg;
                break;

            case 'v':
                printf("%s\n", ccnxPortalServerAbout_Version());
                return 0;

            case 'h':
                usage();
                return 0;

            default:
                usage();
                return -1;
        }
    }

    argc -= optind;
    argv += optind;

    if (argv[0] == NULL || keystoreFile == NULL || keystorePassword == NULL) {
        usage();
        return -1;
    }
    listenName = argv[0];
    commandString = argv[1];
    argc += 2;

    PARCIdentityFile *identityFile = parcIdentityFile_Create(keystoreFile, keystorePassword);

    if (parcIdentityFile_Exists(identityFile) == false) {
        printf("Inaccessible keystore file '%s'.\n", keystoreFile);
        exit(1);
    }

    PARCIdentity *identity = parcIdentity_Create(identityFile, PARCIdentityFileAsPARCIdentity);
    parcIdentityFile_Release(&identityFile);

    CCNxName *name = ccnxName_CreateFromCString(listenName);

    int result = ccnServe(identity, name, commandString);

    ccnxName_Release(&name);

    return result;
}
