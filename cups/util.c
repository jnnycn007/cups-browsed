/*
 * "$Id: util.c,v 1.74 2001/02/07 01:25:39 mike Exp $"
 *
 *   Printing utilities for the Common UNIX Printing System (CUPS).
 *
 *   Copyright 1997-2001 by Easy Software Products.
 *
 *   These coded instructions, statements, and computer programs are the
 *   property of Easy Software Products and are protected by Federal
 *   copyright law.  Distribution and use rights are outlined in the file
 *   "LICENSE.txt" which should have been included with this file.  If this
 *   file is missing or damaged please contact Easy Software Products
 *   at:
 *
 *       Attn: CUPS Licensing Information
 *       Easy Software Products
 *       44141 Airport View Drive, Suite 204
 *       Hollywood, Maryland 20636-3111 USA
 *
 *       Voice: (301) 373-9603
 *       EMail: cups-info@cups.org
 *         WWW: http://www.cups.org
 *
 * Contents:
 *
 *   cupsCancelJob()     - Cancel a print job.
 *   cupsDoFileRequest() - Do an IPP request...
 *   cupsGetClasses()    - Get a list of printer classes.
 *   cupsGetDefault()    - Get the default printer or class.
 *   cupsGetPPD()        - Get the PPD file for a printer.
 *   cupsGetPrinters()   - Get a list of printers.
 *   cupsLastError()     - Return the last IPP error that occurred.
 *   cupsPrintFile()     - Print a file to a printer or class.
 *   cupsPrintFiles()    - Print one or more files to a printer or class.
 *   cupsTempFd()        - Create a temporary file.
 *   cupsTempFile()      - Generate a temporary filename.
 *   cups_connect()      - Connect to the specified host...
 *   cups_local_auth()   - Get the local authorization certificate if
 *                         available/applicable...
 */

/*
 * Include necessary headers...
 */

#include "cups.h"
#include "ipp.h"
#include "language.h"
#include "string.h"
#include "debug.h"
#include <stdlib.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#if defined(WIN32) || defined(__EMX__)
#  include <io.h>
#else
#  include <unistd.h>
#endif /* WIN32 || __EMX__ */


/*
 * Local globals...
 */

static http_t		*cups_server = NULL;	/* Current server connection */
static ipp_status_t	last_error = IPP_OK;	/* Last IPP error */
static char		authstring[1024] = "";	/* Authorization string */


/*
 * Local functions...
 */

static char	*cups_connect(const char *name, char *printer, char *hostname);
static int	cups_local_auth(http_t *http);


/*
 * 'cupsCancelJob()' - Cancel a print job.
 */

int				/* O - 1 on success, 0 on failure */
cupsCancelJob(const char *name,	/* I - Name of printer or class */
              int        job)	/* I - Job ID */
{
  char		printer[HTTP_MAX_URI],	/* Printer name */
		hostname[HTTP_MAX_URI],	/* Hostname */
		uri[HTTP_MAX_URI];	/* Printer URI */
  ipp_t		*request,		/* IPP request */
		*response;		/* IPP response */
  cups_lang_t	*language;		/* Language info */


 /*
  * See if we can connect to the server...
  */

  if (!cups_connect(name, printer, hostname))
  {
    last_error = IPP_SERVICE_UNAVAILABLE;
    return (0);
  }

 /*
  * Build an IPP_CANCEL_JOB request, which requires the following
  * attributes:
  *
  *    attributes-charset
  *    attributes-natural-language
  *    printer-uri
  *    job-id
  *    [requesting-user-name]
  */

  request = ippNew();

  request->request.op.operation_id = IPP_CANCEL_JOB;
  request->request.op.request_id   = 1;

  language = cupsLangDefault();

  ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_CHARSET,
               "attributes-charset", NULL, cupsLangEncoding(language));

  ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_LANGUAGE,
               "attributes-natural-language", NULL,
               language != NULL ? language->language : "C");

  snprintf(uri, sizeof(uri), "ipp://%s:%d/printers/%s", hostname, ippPort(), printer);
  ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_URI, "printer-uri",
               NULL, uri);

  ippAddInteger(request, IPP_TAG_OPERATION, IPP_TAG_INTEGER, "job-id", job);

  ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_NAME, "requesting-user-name",
               NULL, cupsUser());

 /*
  * Do the request...
  */

  if ((response = cupsDoRequest(cups_server, request, "/jobs/")) == NULL)
  {
    last_error = IPP_BAD_REQUEST;
    return (0);
  }
  else
  {
    last_error = response->request.status.status_code;
    ippDelete(response);

    return (1);
  }
}


/*
 * 'cupsDoFileRequest()' - Do an IPP request...
 */

ipp_t *					/* O - Response data */
cupsDoFileRequest(http_t     *http,	/* I - HTTP connection to server */
                  ipp_t      *request,	/* I - IPP request */
                  const char *resource,	/* I - HTTP resource for POST */
		  const char *filename)	/* I - File to send or NULL */
{
  ipp_t		*response;		/* IPP response data */
  char		length[255];		/* Content-Length field */
  http_status_t	status;			/* Status of HTTP request */
  FILE		*file;			/* File to send */
  struct stat	fileinfo;		/* File information */
  int		bytes;			/* Number of bytes read/written */
  char		buffer[8192];		/* Output buffer */
  const char	*password;		/* Password string */
  char		realm[HTTP_MAX_VALUE],	/* realm="xyz" string */
		nonce[HTTP_MAX_VALUE],	/* nonce="xyz" string */
		plain[255],		/* Plaintext username:password */
		encode[255];		/* Encoded username:password */
  char		prompt[1024];		/* Prompt string */


  if (http == NULL || request == NULL || resource == NULL)
  {
    if (request != NULL)
      ippDelete(request);

    last_error = IPP_INTERNAL_ERROR;
    return (NULL);
  }

  DEBUG_printf(("cupsDoFileRequest(%p, %08x, \'%s\', \'%s\')\n",
                http, request, resource, filename ? filename : "(null)"));

 /*
  * See if we have a file to send...
  */

  if (filename != NULL)
  {
    if (stat(filename, &fileinfo))
    {
     /*
      * Can't get file information!
      */

      ippDelete(request);
      last_error = IPP_NOT_FOUND;
      return (NULL);
    }

    if ((file = fopen(filename, "rb")) == NULL)
    {
     /*
      * Can't open file!
      */

      ippDelete(request);
      last_error = IPP_NOT_FOUND;
      return (NULL);
    }
  }
  else
    file = NULL;

 /*
  * Loop until we can send the request without authorization problems.
  */

  response = NULL;
  status   = HTTP_ERROR;

  while (response == NULL)
  {
    DEBUG_puts("cupsDoFileRequest: setup...");

   /*
    * Setup the HTTP variables needed...
    */

    if (filename != NULL)
      sprintf(length, "%u", ippLength(request) + (size_t)fileinfo.st_size);
    else
      sprintf(length, "%u", ippLength(request));

    httpClearFields(http);
    httpSetField(http, HTTP_FIELD_CONTENT_LENGTH, length);
    httpSetField(http, HTTP_FIELD_CONTENT_TYPE, "application/ipp");
    httpSetField(http, HTTP_FIELD_AUTHORIZATION, authstring);

   /*
    * Try the request...
    */

    DEBUG_puts("cupsDoFileRequest: post...");

    if (httpPost(http, resource))
      continue;

   /*
    * Send the IPP data and wait for the response...
    */

    DEBUG_puts("cupsDoFileRequest: ipp write...");

    request->state = IPP_IDLE;
    if (ippWrite(http, request) != IPP_ERROR)
      if (filename != NULL)
      {
        DEBUG_puts("cupsDoFileRequest: file write...");

       /*
        * Send the file...
        */

        rewind(file);

        while ((bytes = fread(buffer, 1, sizeof(buffer), file)) > 0)
  	  if (httpWrite(http, buffer, bytes) < bytes)
            break;
      }

   /*
    * Get the server's return status...
    */

    DEBUG_puts("cupsDoFileRequest: update...");

    while ((status = httpUpdate(http)) == HTTP_CONTINUE);

    if (status == HTTP_UNAUTHORIZED)
    {
      DEBUG_puts("cupsDoFileRequest: unauthorized...");

     /*
      * Flush any error message...
      */

      httpFlush(http);

     /*
      * See if we can do local authentication...
      */

      if (cups_local_auth(http))
        continue;

     /*
      * Nope - get a password from the user...
      */

      snprintf(prompt, sizeof(prompt), "Password for %s on %s? ", cupsUser(),
               http->hostname);

      if ((password = cupsGetPassword(prompt)) != NULL)
      {
       /*
	* Got a password; send it to the server...
	*/

        if (!password[0])
          break;

        if (strncmp(http->fields[HTTP_FIELD_WWW_AUTHENTICATE], "Basic", 5) == 0)
        {
	 /*
	  * Basic authentication...
	  */

	  snprintf(plain, sizeof(plain), "%s:%s", cupsUser(), password);
	  httpEncode64(encode, plain);
	  snprintf(authstring, sizeof(authstring), "Basic %s", encode);
	}
        else
	{
	 /*
	  * Digest authentication...
	  */

          httpGetSubField(http, HTTP_FIELD_WWW_AUTHENTICATE, "realm", realm);
          httpGetSubField(http, HTTP_FIELD_WWW_AUTHENTICATE, "nonce", nonce);

	  httpMD5(cupsUser(), realm, password, encode);
	  httpMD5Final(nonce, "POST", resource, encode);
	  snprintf(authstring, sizeof(authstring),
	           "Digest username=\"%s\", realm=\"%s\", nonce=\"%s\", "
	           "response=\"%s\"", cupsUser(), realm, nonce, encode);
	}
        continue;
      }
      else
        break;
    }
    else if (status == HTTP_ERROR)
    {
#if defined(WIN32) || defined(__EMX__)
      if (http->error != WSAENETDOWN && http->error != WSAENETUNREACH)
#else
      if (http->error != ENETDOWN && http->error != ENETUNREACH)
#endif /* WIN32 || __EMX__ */
        continue;
      else
        break;
    }
    else if (status != HTTP_OK)
    {
      DEBUG_printf(("cupsDoFileRequest: error %d...\n", status));

     /*
      * Flush any error message...
      */

      httpFlush(http);
      break;
    }
    else
    {
     /*
      * Read the response...
      */

      DEBUG_puts("cupsDoFileRequest: response...");

      response = ippNew();

      if (ippRead(http, response) == IPP_ERROR)
      {
       /*
        * Delete the response...
	*/

	ippDelete(response);
	response = NULL;

        last_error = IPP_SERVICE_UNAVAILABLE;

       /*
	* Flush any remaining data...
	*/

	httpFlush(http);
	break;
      }
    }
  }

 /*
  * Close the file if needed...
  */

  if (filename != NULL)
    fclose(file);

 /*
  * Delete the original request and return the response...
  */
  
  ippDelete(request);

  if (response)
    last_error = response->request.status.status_code;
  else if (status == HTTP_NOT_FOUND)
    last_error = IPP_NOT_FOUND;
  else if (status == HTTP_UNAUTHORIZED)
    last_error = IPP_NOT_AUTHORIZED;
  else if (status != HTTP_OK)
    last_error = IPP_SERVICE_UNAVAILABLE;

  return (response);
}


/*
 * 'cupsGetClasses()' - Get a list of printer classes.
 */

int				/* O - Number of classes */
cupsGetClasses(char ***classes)	/* O - Classes */
{
  int		n;		/* Number of classes */
  ipp_t		*request,	/* IPP Request */
		*response;	/* IPP Response */
  ipp_attribute_t *attr;	/* Current attribute */
  cups_lang_t	*language;	/* Default language */
  char		**temp;		/* Temporary pointer */


  if (classes == NULL)
  {
    last_error = IPP_INTERNAL_ERROR;
    return (0);
  }

 /*
  * Try to connect to the server...
  */

  if (!cups_connect("default", NULL, NULL))
  {
    last_error = IPP_SERVICE_UNAVAILABLE;
    return (0);
  }

 /*
  * Build a CUPS_GET_CLASSES request, which requires the following
  * attributes:
  *
  *    attributes-charset
  *    attributes-natural-language
  *    requested-attributes
  */

  request = ippNew();

  request->request.op.operation_id = CUPS_GET_CLASSES;
  request->request.op.request_id   = 1;

  language = cupsLangDefault();

  ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_CHARSET,
               "attributes-charset", NULL, cupsLangEncoding(language));

  ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_LANGUAGE,
               "attributes-natural-language", NULL, language->language);

  ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_KEYWORD,
               "requested-attributes", NULL, "printer-name");

 /*
  * Do the request and get back a response...
  */

  n        = 0;
  *classes = NULL;

  if ((response = cupsDoRequest(cups_server, request, "/")) != NULL)
  {
    last_error = response->request.status.status_code;

    for (attr = response->attrs; attr != NULL; attr = attr->next)
      if (attr->name != NULL &&
          strcasecmp(attr->name, "printer-name") == 0 &&
          attr->value_tag == IPP_TAG_NAME)
      {
        if (n == 0)
	  temp = malloc(sizeof(char *));
	else
	  temp = realloc(*classes, sizeof(char *) * (n + 1));

	if (temp == NULL)
	{
	 /*
	  * Ran out of memory!
	  */

          while (n > 0)
	  {
	    n --;
	    free((*classes)[n]);
	  }

	  free(*classes);
	  ippDelete(response);
	  return (0);
	}

        *classes = temp;
        temp[n]  = strdup(attr->values[0].string.text);
	n ++;
      }

    ippDelete(response);
  }
  else
    last_error = IPP_BAD_REQUEST;

  return (n);
}


/*
 * 'cupsGetDefault()' - Get the default printer or class.
 */

const char *			/* O - Default printer or NULL */
cupsGetDefault(void)
{
  ipp_t		*request,	/* IPP Request */
		*response;	/* IPP Response */
  ipp_attribute_t *attr;	/* Current attribute */
  cups_lang_t	*language;	/* Default language */
  const char	*var;		/* Environment variable */
  static char	def_printer[256];/* Default printer */


 /*
  * First see if the LPDEST or PRINTER environment variables are
  * set...  However, if PRINTER is set to "lp", ignore it to work
  * around a "feature" in most Linux distributions - the default
  * user login scripts set PRINTER to "lp"...
  */

  if ((var = getenv("LPDEST")) != NULL)
    return (var);
  else if ((var = getenv("PRINTER")) != NULL && strcmp(var, "lp") != 0)
    return (var);

 /*
  * Try to connect to the server...
  */

  if (!cups_connect("default", NULL, NULL))
  {
    last_error = IPP_SERVICE_UNAVAILABLE;
    return (NULL);
  }

 /*
  * Build a CUPS_GET_DEFAULT request, which requires the following
  * attributes:
  *
  *    attributes-charset
  *    attributes-natural-language
  */

  request = ippNew();

  request->request.op.operation_id = CUPS_GET_DEFAULT;
  request->request.op.request_id   = 1;

  language = cupsLangDefault();

  ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_CHARSET,
               "attributes-charset", NULL, cupsLangEncoding(language));

  ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_LANGUAGE,
               "attributes-natural-language", NULL, language->language);

 /*
  * Do the request and get back a response...
  */

  if ((response = cupsDoRequest(cups_server, request, "/")) != NULL)
  {
    last_error = response->request.status.status_code;

    if ((attr = ippFindAttribute(response, "printer-name", IPP_TAG_NAME)) != NULL)
    {
      strncpy(def_printer, attr->values[0].string.text, sizeof(def_printer) - 1);
      def_printer[sizeof(def_printer) - 1] = '\0';
      ippDelete(response);
      return (def_printer);
    }

    ippDelete(response);
  }
  else
    last_error = IPP_BAD_REQUEST;

  return (NULL);
}


/*
 * 'cupsGetPPD()' - Get the PPD file for a printer.
 */

const char *				/* O - Filename for PPD file */
cupsGetPPD(const char *name)		/* I - Printer name */
{
  int		i;			/* Looping var */
  ipp_t		*request,		/* IPP request */
		*response;		/* IPP response */
  ipp_attribute_t *attr;		/* Current attribute */
  cups_lang_t	*language;		/* Local language */
  int		fd;			/* PPD file */
  int		bytes;			/* Number of bytes read */
  char		buffer[8192];		/* Buffer for file */
  char		printer[HTTP_MAX_URI],	/* Printer name */
		method[HTTP_MAX_URI],	/* Method/scheme name */
		username[HTTP_MAX_URI],	/* Username:password */
		hostname[HTTP_MAX_URI],	/* Hostname */
		resource[HTTP_MAX_URI];	/* Resource name */
  int		port;			/* Port number */
  const char	*password;		/* Password string */
  char		realm[HTTP_MAX_VALUE],	/* realm="xyz" string */
		nonce[HTTP_MAX_VALUE],	/* nonce="xyz" string */
		plain[255],		/* Plaintext username:password */
		encode[255];		/* Encoded username:password */
  http_status_t	status;			/* HTTP status from server */
  char		prompt[1024];		/* Prompt string */
  static char	filename[HTTP_MAX_URI];	/* Local filename */
  static const char *requested_attrs[] =/* Requested attributes */
		{
		  "printer-uri-supported",
		  "printer-type",
		  "member-uris"
		};


  if (name == NULL)
  {
    last_error = IPP_INTERNAL_ERROR;
    return (NULL);
  }

 /*
  * See if we can connect to the server...
  */

  if (!cups_connect(name, printer, hostname))
  {
    last_error = IPP_SERVICE_UNAVAILABLE;
    return (NULL);
  }

 /*
  * Build an IPP_GET_PRINTER_ATTRIBUTES request, which requires the following
  * attributes:
  *
  *    attributes-charset
  *    attributes-natural-language
  *    printer-uri
  *    requested-attributes
  */

  request = ippNew();

  request->request.op.operation_id = IPP_GET_PRINTER_ATTRIBUTES;
  request->request.op.request_id   = 1;

  language = cupsLangDefault();

  ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_CHARSET,
               "attributes-charset", NULL, cupsLangEncoding(language));

  ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_LANGUAGE,
               "attributes-natural-language", NULL, language->language);

  snprintf(buffer, sizeof(buffer), "ipp://localhost/printers/%s", name);
  ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_URI,
               "printer-uri", NULL, buffer);

  ippAddStrings(request, IPP_TAG_OPERATION, IPP_TAG_NAME,
                "requested-attributes",
		sizeof(requested_attrs) / sizeof(requested_attrs[0]),
		NULL, requested_attrs);

 /*
  * Do the request and get back a response...
  */

  if ((response = cupsDoRequest(cups_server, request, "/")) != NULL)
  {
    last_error  = response->request.status.status_code;
    printer[0]  = '\0';
    hostname[0] = '\0';

    if ((attr = ippFindAttribute(response, "member-uris", IPP_TAG_URI)) != NULL)
    {
     /*
      * Get the first actual server and printer name in the class...
      */

      for (i = 0; i < attr->num_values; i ++)
      {
	httpSeparate(attr->values[0].string.text, method, username, hostname,
	             &port, resource);
	if (strncmp(resource, "/printers/", 10) == 0)
	{
	 /*
	  * Found a printer!
	  */

	  strncpy(printer, resource + 10, sizeof(printer) - 1);
	  printer[sizeof(printer) - 1] = '\0';
	  break;
	}
      }
    }
    else if ((attr = ippFindAttribute(response, "printer-uri-supported",
                                      IPP_TAG_URI)) != NULL)
    {
     /*
      * Get the actual server and printer names...
      */

      httpSeparate(attr->values[0].string.text, method, username, hostname,
	           &port, resource);
      strncpy(printer, strrchr(resource, '/') + 1, sizeof(printer) - 1);
      printer[sizeof(printer) - 1] = '\0';
    }

    ippDelete(response);

   /*
    * Remap local hostname to localhost...
    */

    gethostname(buffer, sizeof(buffer));

    if (strcasecmp(buffer, hostname) == 0)
      strcpy(hostname, "localhost");
  }

  cupsLangFree(language);

  if (!printer[0])
    return (NULL);

 /*
  * Reconnect to the correct server as needed...
  */

  if (strcasecmp(cups_server->hostname, hostname) != 0)
  {
    httpClose(cups_server);

    if ((cups_server = httpConnect(hostname, ippPort())) == NULL)
    {
      last_error = IPP_SERVICE_UNAVAILABLE;
      return (NULL);
    }
  }

 /*
  * Get a temp file...
  */

  if ((fd = cupsTempFd(filename, sizeof(filename))) < 0)
  {
   /*
    * Can't open file; close the server connection and return NULL...
    */

    httpFlush(cups_server);
    httpClose(cups_server);
    cups_server = NULL;
    return (NULL);
  }

 /*
  * And send a request to the HTTP server...
  */

  snprintf(resource, sizeof(resource), "/printers/%s.ppd", printer);

  do
  {
    httpClearFields(cups_server);
    httpSetField(cups_server, HTTP_FIELD_HOST, hostname);
    httpSetField(cups_server, HTTP_FIELD_AUTHORIZATION, authstring);

    if (httpGet(cups_server, resource))
    {
      httpReconnect(cups_server);
      status = HTTP_UNAUTHORIZED;
      continue;
    }

    while ((status = httpUpdate(cups_server)) == HTTP_CONTINUE);

    if (status == HTTP_UNAUTHORIZED)
    {
      DEBUG_puts("cupsGetPPD: unauthorized...");

     /*
      * Flush any error message...
      */

      httpFlush(cups_server);

     /*
      * See if we can do local authentication...
      */

      if (cups_local_auth(cups_server))
        continue;

     /*
      * Nope, get a password from the user...
      */

      snprintf(prompt, sizeof(prompt), "Password for %s on %s? ", cupsUser(),
               cups_server->hostname);

      if ((password = cupsGetPassword(prompt)) != NULL)
      {
       /*
	* Got a password; send it to the server...
	*/

        if (!password[0])
          break;

        if (strncmp(cups_server->fields[HTTP_FIELD_WWW_AUTHENTICATE], "Basic", 5) == 0)
        {
	 /*
	  * Basic authentication...
	  */

	  snprintf(plain, sizeof(plain), "%s:%s", cupsUser(), password);
	  httpEncode64(encode, plain);
	  snprintf(authstring, sizeof(authstring), "Basic %s", encode);
	}
        else
	{
	 /*
	  * Digest authentication...
	  */

          httpGetSubField(cups_server, HTTP_FIELD_WWW_AUTHENTICATE, "realm", realm);
          httpGetSubField(cups_server, HTTP_FIELD_WWW_AUTHENTICATE, "nonce", nonce);

	  httpMD5(cupsUser(), realm, password, encode);
	  httpMD5Final(nonce, "GET", resource, encode);
	  snprintf(authstring, sizeof(authstring),
	           "Digest username=\"%s\", realm=\"%s\", nonce=\"%s\", "
	           "response=\"%s\"", cupsUser(), realm, nonce, encode);
	}
        continue;
      }
      else
        break;
    }
  }
  while (status == HTTP_UNAUTHORIZED);

 /*
  * See if we actually got the file or an error...
  */

  if (status != HTTP_OK)
  {
    unlink(filename);
    httpFlush(cups_server);
    httpClose(cups_server);
    cups_server = NULL;
    return (NULL);
  }

 /*
  * OK, we need to copy the file...
  */

  while ((bytes = httpRead(cups_server, buffer, sizeof(buffer))) > 0)
    write(fd, buffer, bytes);

  close(fd);

  return (filename);
}


/*
 * 'cupsGetPrinters()' - Get a list of printers.
 */

int					/* O - Number of printers */
cupsGetPrinters(char ***printers)	/* O - Printers */
{
  int		n;		/* Number of printers */
  ipp_t		*request,	/* IPP Request */
		*response;	/* IPP Response */
  ipp_attribute_t *attr;	/* Current attribute */
  cups_lang_t	*language;	/* Default language */
  char		**temp;		/* Temporary pointer */


  if (printers == NULL)
  {
    last_error = IPP_INTERNAL_ERROR;
    return (0);
  }

 /*
  * Try to connect to the server...
  */

  if (!cups_connect("default", NULL, NULL))
  {
    last_error = IPP_SERVICE_UNAVAILABLE;
    return (0);
  }

 /*
  * Build a CUPS_GET_PRINTERS request, which requires the following
  * attributes:
  *
  *    attributes-charset
  *    attributes-natural-language
  *    requested-attributes
  */

  request = ippNew();

  request->request.op.operation_id = CUPS_GET_PRINTERS;
  request->request.op.request_id   = 1;

  language = cupsLangDefault();

  ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_CHARSET,
               "attributes-charset", NULL, cupsLangEncoding(language));

  ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_LANGUAGE,
               "attributes-natural-language", NULL, language->language);

  ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_KEYWORD,
               "requested-attributes", NULL, "printer-name");

 /*
  * Do the request and get back a response...
  */

  n         = 0;
  *printers = NULL;

  if ((response = cupsDoRequest(cups_server, request, "/")) != NULL)
  {
    last_error = response->request.status.status_code;

    for (attr = response->attrs; attr != NULL; attr = attr->next)
      if (attr->name != NULL &&
          strcasecmp(attr->name, "printer-name") == 0 &&
          attr->value_tag == IPP_TAG_NAME)
      {
        if (n == 0)
	  temp = malloc(sizeof(char *));
	else
	  temp = realloc(*printers, sizeof(char *) * (n + 1));

	if (temp == NULL)
	{
	 /*
	  * Ran out of memory!
	  */

	  while (n > 0)
	  {
	    n --;
	    free((*printers)[n]);
	  }

	  free(*printers);
	  ippDelete(response);
	  return (0);
	}

        *printers = temp;
        temp[n]   = strdup(attr->values[0].string.text);
	n ++;
      }

    ippDelete(response);
  }
  else
    last_error = IPP_BAD_REQUEST;

  return (n);
}


/*
 * 'cupsLastError()' - Return the last IPP error that occurred.
 */

ipp_status_t		/* O - IPP error code */
cupsLastError(void)
{
  return (last_error);
}


/*
 * 'cupsPrintFile()' - Print a file to a printer or class.
 */

int					/* O - Job ID */
cupsPrintFile(const char    *name,	/* I - Printer or class name */
              const char    *filename,	/* I - File to print */
	      const char    *title,	/* I - Title of job */
              int           num_options,/* I - Number of options */
	      cups_option_t *options)	/* I - Options */
{
  DEBUG_printf(("cupsPrintFile(\'%s\', \'%s\', %d, %p)\n",
                name, filename, num_options, options));

  return (cupsPrintFiles(name, 1, &filename, title, num_options, options));
}


/*
 * 'cupsPrintFiles()' - Print one or more files to a printer or class.
 */

int					/* O - Job ID */
cupsPrintFiles(const char    *name,	/* I - Printer or class name */
               int           num_files,	/* I - Number of files */
               const char    **files,	/* I - File(s) to print */
	       const char    *title,	/* I - Title of job */
               int           num_options,/* I - Number of options */
	       cups_option_t *options)	/* I - Options */
{
  int		i;			/* Looping var */
  const char	*val;			/* Pointer to option value */
  ipp_t		*request;		/* IPP request */
  ipp_t		*response;		/* IPP response */
  ipp_attribute_t *attr;		/* IPP job-id attribute */
  char		hostname[HTTP_MAX_URI],	/* Hostname */
		printer[HTTP_MAX_URI],	/* Printer or class name */
		uri[HTTP_MAX_URI];	/* Printer URI */
  cups_lang_t	*language;		/* Language to use */
  int		jobid;			/* New job ID */


  DEBUG_printf(("cupsPrintFiles(\'%s\', %d, %p, %d, %p)\n",
                name, num_files, files, num_options, options));

  if (name == NULL || num_files < 1 || files == NULL)
    return (0);

 /*
  * Setup a connection and request data...
  */

  if (!cups_connect(name, printer, hostname))
  {
    DEBUG_printf(("cupsPrintFile: Unable to open connection - %s.\n",
                  strerror(errno)));
    last_error = IPP_SERVICE_UNAVAILABLE;
    return (0);
  }

  language = cupsLangDefault();

 /*
  * Build a standard CUPS URI for the printer and fill the standard IPP
  * attributes...
  */

  if ((request = ippNew()) == NULL)
    return (0);

  request->request.op.operation_id = num_files == 1 ? IPP_PRINT_JOB :
                                                      IPP_CREATE_JOB;
  request->request.op.request_id   = 1;

  snprintf(uri, sizeof(uri), "ipp://%s:%d/printers/%s", hostname, ippPort(), printer);

  ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_CHARSET,
               "attributes-charset", NULL, cupsLangEncoding(language));

  ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_LANGUAGE,
               "attributes-natural-language", NULL,
               language != NULL ? language->language : "C");

  ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_URI, "printer-uri",
               NULL, uri);

  ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_NAME, "requesting-user-name",
               NULL, cupsUser());

  if (title)
    ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_NAME, "job-name", NULL, title);

 /*
  * Then add all options...
  */

  cupsEncodeOptions(request, num_options, options);

 /*
  * Do the request...
  */

  snprintf(uri, sizeof(uri), "/printers/%s", printer);

  if (num_files == 1)
    response = cupsDoFileRequest(cups_server, request, uri, *files);
  else
    response = cupsDoRequest(cups_server, request, uri);

  if (response == NULL)
    jobid = 0;
  else if (response->request.status.status_code > IPP_OK_CONFLICT)
  {
    DEBUG_printf(("IPP response code was 0x%x!\n",
                  response->request.status.status_code));
    jobid = 0;
  }
  else if ((attr = ippFindAttribute(response, "job-id", IPP_TAG_INTEGER)) == NULL)
  {
    DEBUG_puts("No job ID!");
    jobid = 0;
  }
  else
    jobid = attr->values[0].integer;

  if (response != NULL)
    ippDelete(response);

 /*
  * Handle multiple file jobs if the create-job operation worked...
  */

  if (jobid > 0 && num_files > 1)
    for (i = 0; i < num_files; i ++)
    {
     /*
      * Build a standard CUPS URI for the job and fill the standard IPP
      * attributes...
      */

      if ((request = ippNew()) == NULL)
	return (0);

      request->request.op.operation_id = IPP_SEND_DOCUMENT;
      request->request.op.request_id   = 1;

      snprintf(uri, sizeof(uri), "ipp://%s:%d/jobs/%d", hostname, ippPort(),
               jobid);

      ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_CHARSET,
        	   "attributes-charset", NULL, cupsLangEncoding(language));

      ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_LANGUAGE,
        	   "attributes-natural-language", NULL,
        	   language != NULL ? language->language : "C");

      ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_URI, "job-uri",
        	   NULL, uri);

     /*
      * Handle raw print files...
      */

      if (cupsGetOption("raw", num_options, options))
	ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_MIMETYPE, "document-format",
        	     NULL, "application/vnd.cups-raw");
      else if ((val = cupsGetOption("document-format", num_options, options)) != NULL)
	ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_MIMETYPE, "document-format",
        	     NULL, val);
      else
	ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_MIMETYPE, "document-format",
        	     NULL, "application/octet-stream");

      ippAddString(request, IPP_TAG_OPERATION, IPP_TAG_NAME, "requesting-user-name",
        	   NULL, cupsUser());

     /*
      * Is this the last document?
      */

      if (i == (num_files - 1))
        ippAddBoolean(request, IPP_TAG_OPERATION, "last-document", 1);

     /*
      * Send the file...
      */

      snprintf(uri, sizeof(uri), "/printers/%s", printer);

      if ((response = cupsDoFileRequest(cups_server, request, uri,
                                        files[i])) != NULL)
	ippDelete(response);
    }

  return (jobid);
}


/*
 * 'cupsTempFd()' - Create a temporary file.
 */

int					/* O - New file descriptor */
cupsTempFd(char *filename,		/* I - Pointer to buffer */
           int  len)			/* I - Size of buffer */
{
  int		fd;			/* File descriptor for temp file */
#ifdef WIN32
  char		tmpdir[1024];		/* Windows temporary directory */
  DWORD		curtime;		/* Current time */
#else
  char		*tmpdir;		/* TMPDIR environment var */
  struct timeval curtime;		/* Current time */
#endif /* WIN32 */
  static char	buf[1024] = "";		/* Buffer if you pass in NULL and 0 */


 /*
  * See if a filename was specified...
  */

  if (filename == NULL)
  {
    filename = buf;
    len      = sizeof(buf);
  }

 /*
  * See if TMPDIR is defined...
  */

#ifdef WIN32
  GetTempPath(sizeof(tmpdir), tmpdir);
#else
  if ((tmpdir = getenv("TMPDIR")) == NULL)
  {
   /*
    * Put root temp files in restricted temp directory...
    */

    if (getuid() == 0)
      tmpdir = CUPS_REQUESTS "/tmp";
    else
      tmpdir = "/var/tmp";
  }
#endif /* WIN32 */

 /*
  * Make the temporary name using the specified directory...
  */

  do
  {
#ifdef WIN32
   /*
    * Get the current time of day...
    */

    curtime = GetTickCount();

   /*
    * Format a string using the hex time values...
    */

    snprintf(filename, len - 1, "%s/%08lx", tmpdir, curtime);
#else
   /*
    * Get the current time of day...
    */

    gettimeofday(&curtime, NULL);

   /*
    * Format a string using the hex time values...
    */

    snprintf(filename, len - 1, "%s/%08lx%05lx", tmpdir,
             curtime.tv_sec, curtime.tv_usec);
#endif /* WIN32 */

   /*
    * Open the file in "exclusive" mode, making sure that we don't
    * stomp on an existing file or someone's symlink crack...
    */

#ifdef O_NOFOLLOW
    fd = open(filename, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0600);
#else
    fd = open(filename, O_WRONLY | O_CREAT | O_EXCL, 0600);
#endif /* O_NOFOLLOW */

    if (fd < 0 && errno == EPERM)
      break; /* Stop immediately if permission denied! */
  }
  while (fd < 0);

 /*
  * Return the file descriptor...
  */

  return (fd);
}


/*
 * 'cupsTempFile()' - Generate a temporary filename.
 *
 * Note: This function will be removed in the next minor release of
 *       CUPS due to serious bugs in glibc - the fopen() function
 *       in glibc does an unlink before creating the file, which
 *       opens up a major symlink hole!
 */

char *					/* O - Filename */
cupsTempFile(char *filename,		/* I - Pointer to buffer */
             int  len)			/* I - Size of buffer */
{
  int		fd;			/* File descriptor for temp file */


 /*
  * Create the temporary file...
  */

  if ((fd = cupsTempFd(filename, len)) < 0)
    return (NULL);

 /*
  * Close the temp file - it'll be reopened later as needed...
  */

  close(fd);

 /*
  * Return the temp filename...
  */

  return (filename);
}


/*
 * 'cups_connect()' - Connect to the specified host...
 */

static char *				/* I - Printer name or NULL */
cups_connect(const char *name,		/* I - Destination (printer[@host]) */
	     char       *printer,	/* O - Printer name [HTTP_MAX_URI] */
             char       *hostname)	/* O - Hostname [HTTP_MAX_URI] */
{
  char		hostbuf[HTTP_MAX_URI];	/* Name of host */
  static char	printerbuf[HTTP_MAX_URI];
					/* Name of printer or class */


  if (name == NULL)
  {
    last_error = IPP_BAD_REQUEST;
    return (NULL);
  }

  if (sscanf(name, "%1023[^@]@%1023s", printerbuf, hostbuf) == 1)
  {
    strncpy(hostbuf, cupsServer(), sizeof(hostbuf) - 1);
    hostbuf[sizeof(hostbuf) - 1] = '\0';
  }

  if (hostname != NULL)
  {
    strncpy(hostname, hostbuf, HTTP_MAX_URI - 1);
    hostname[HTTP_MAX_URI - 1] = '\0';
  }
  else
    hostname = hostbuf;

  if (printer != NULL)
  {
    strncpy(printer, printerbuf, HTTP_MAX_URI - 1);
    printer[HTTP_MAX_URI - 1] = '\0';
  }
  else
    printer = printerbuf;

  if (cups_server != NULL)
  {
    if (strcasecmp(cups_server->hostname, hostname) == 0)
      return (printer);

    httpClose(cups_server);
  }

  if ((cups_server = httpConnect(hostname, ippPort())) == NULL)
  {
    last_error = IPP_SERVICE_UNAVAILABLE;
    return (NULL);
  }
  else
  {
    httpEncryption(cups_server, cupsEncryption());
    return (printer);
  }
}


/*
 * 'cups_local_auth()' - Get the local authorization certificate if
 *                       available/applicable...
 */

static int			/* O - 1 if available, 0 if not */
cups_local_auth(http_t *http)	/* I - Connection */
{
#if defined(WIN32) || defined(__EMX__)
 /*
  * Currently WIN32 and OS-2 do not support the CUPS server...
  */

  return (0);
#else
  int		pid;		/* Current process ID */
  FILE		*fp;		/* Certificate file */
  char		filename[1024],	/* Certificate filename */
		certificate[33];/* Certificate string */
  const char	*root;		/* Server root directory */


 /*
  * See if we are accessing localhost...
  */

  if (ntohl(http->hostaddr.sin_addr.s_addr) != 0x7f000001 &&
      strcasecmp(http->hostname, "localhost") != 0)
    return (0);

 /*
  * Try opening a certificate file for this PID.  If that fails,
  * try the root certificate...
  */

  if ((root = getenv("CUPS_SERVERROOT")) == NULL)
    root = CUPS_SERVERROOT;

  pid = getpid();
  snprintf(filename, sizeof(filename), "%s/certs/%d", root, pid);
  if ((fp = fopen(filename, "r")) == NULL && pid > 0)
  {
    snprintf(filename, sizeof(filename), "%s/certs/0", root);
    fp = fopen(filename, "r");
  }

  if (fp == NULL)
    return (0);

 /*
  * Read the certificate from the file...
  */

  fgets(certificate, sizeof(certificate), fp);
  fclose(fp);

 /*
  * Set the authorization string and return...
  */

  snprintf(authstring, sizeof(authstring), "Local %s", certificate);

  return (1);
#endif /* WIN32 || __EMX__ */
}


/*
 * End of "$Id: util.c,v 1.74 2001/02/07 01:25:39 mike Exp $".
 */
