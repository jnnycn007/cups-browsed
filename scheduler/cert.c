/*
 * "$Id: cert.c,v 1.1 2000/01/20 03:51:33 mike Exp $"
 *
 *   Authentication certificate routines for the Common UNIX
 *   Printing System (CUPS).
 *
 *   Copyright 1997-2000 by Easy Software Products.
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
 *   AddCert()        - Add a certificate.
 *   DeleteCert()     - Delete a single certificate.
 *   DeleteAllCerts() - Delete all certificates...
 *   FindCert()       - Find a certificate.
 *   InitCerts()      - Initialize the certificate "system" and root
 *                      certificate.
 */

/*
 * Include necessary headers...
 */

#include "cupsd.h"
#include <grp.h>


/*
 * 'AddCert()' - Add a certificate.
 */

void
AddCert(int        pid,			/* I - Process ID */
        const char *username)		/* I - Username */
{
  int		i;			/* Looping var */
  cert_t	*cert;			/* Current certificate */
  FILE		*fp;			/* Certificate file */
  char		filename[1024];		/* Certificate filename */
  struct group	*grp;			/* System group */
  static const char *hex = "0123456789ABCDEF";
					/* Hex constants... */


 /*
  * Allocate memory for the certificate...
  */

  if ((cert = calloc(sizeof(cert_t), 1)) == NULL)
    return;

 /*
  * Fill in the certificate information...
  */

  cert->pid = pid;
  strncpy(cert->username, username, sizeof(cert->username) - 1);

  for (i = 0; i < 32; i ++)
    cert->certificate[i] = hex[random() & 15];

 /*
  * Save the certificate to a file readable only by the User and Group
  * (or root and SystemGroup for PID == 0)...
  */

  sprintf(filename, "%s/certs/%d", ServerRoot, pid);

  if ((fp = fopen(filename, "w")) == NULL)
  {
    free(cert);
    return;
  }

  if (pid == 0)
  {
   /*
    * Root certificate...
    */

    fchmod(fileno(fp), 0440);

    if ((grp = getgrnam(SystemGroup)) == NULL)
      fchown(fileno(fp), 0, 0);
    else
      fchown(fileno(fp), 0, grp->gr_gid);

    endgrent();

    RootCertTime = time(NULL);
  }
  else
  {
   /*
    * CGI certificate...
    */

    fchmod(fileno(fp), 0400);
    fchown(fileno(fp), User, Group);
  }

  fputs(cert->certificate, fp);
  fclose(fp);

 /*
  * Insert the certificate at the front of the list...
  */

  cert->next = Certs;
  Certs      = cert;
}


/*
 * 'DeleteCert()' - Delete a single certificate.
 */

void
DeleteCert(int pid)			/* I - Process ID */
{
  cert_t	*cert,			/* Current certificate */
		*prev;			/* Previous certificate */
  char		filename[1024];		/* Certificate file */


  for (prev = NULL, cert = Certs; cert != NULL; prev = cert, cert = cert->next)
    if (cert->pid == pid)
    {
     /*
      * Remove this certificate from the list...
      */

      if (prev == NULL)
        Certs = cert->next;
      else
        prev->next = cert->next;

      free(cert);

     /*
      * Delete the file and return...
      */

      sprintf(filename, "%s/certs/%d", ServerRoot, pid);
      unlink(filename);
      return;
    }
}


/*
 * 'DeleteAllCerts()' - Delete all certificates...
 */

void
DeleteAllCerts(void)
{
  cert_t	*cert,			/* Current certificate */
		*next;			/* Next certificate */
  char		filename[1024];		/* Certificate file */


 /*
  * Loop through each certificate, deleting them...
  */

  for (cert = Certs; cert != NULL; cert = next)
  {
   /*
    * Delete the file...
    */

    sprintf(filename, "%s/certs/%d", ServerRoot, cert->pid);
    unlink(filename);

   /*
    * Free memory...
    */

    next = cert->next;
    free(cert);
  }

  Certs = NULL;
}


/*
 * 'FindCert()' - Find a certificate.
 */

const char *				/* O - Matching username or NULL */
FindCert(const char *certificate)	/* I - Certificate */
{
  cert_t	*cert;			/* Current certificate */


  for (cert = Certs; cert != NULL; cert = cert->next)
    if (strcasecmp(certificate, cert->certificate) == 0)
      return (cert->username);

  return (NULL);
}


/*
 * 'InitCerts()' - Initialize the certificate "system" and root certificate.
 */

void
InitCerts(void)
{
  struct timeval	tod;	/* Time of day */


 /*
  * Initialize the random number generator using the current time,
  * including milliseconds...
  */

  gettimeofday(&tod, NULL);

  srandom(tod.tv_sec + tod.tv_usec);

 /*
  * Create a root certificate and return...
  */

  AddCert(0, "root");
}


/*
 * End of "$Id: cert.c,v 1.1 2000/01/20 03:51:33 mike Exp $".
 */
