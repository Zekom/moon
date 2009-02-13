/*
 * downloader.cpp: Downloader class.
 *
 * The downloader implements two modes of operation:
 *
 *    bare bones:  this is the interface expected by Javascript and C#
 *		 this is the default if the caller does not call
 *		 Downloader::SetWriteFunc
 * 
 *    progressive: this interface is used internally by the Image
 *		 class to do progressive loading.   If you want to
 *		 use this mode, you must call the SetWriteFunc routine
 *		 to install your callbacks before starting the download.
 * 
 * TODO:
 *    Need a mechanism to notify the managed client of errors during 
 *    download.
 *
 *    Need to provide the buffer we downloaded to GetResponseText(string PartName)
 *    so we can return the response text for the given part name.
 *
 *    The providers should store the files *somewhere* and should be able
 *    to respond to the "GetResponsetext" above on demand.   The current
 *    code in demo.cpp and ManagedDownloader are not complete in this regard as
 *    they only stream
 *
 * Contact:
 *   Moonlight List (moonlight-list@lists.ximian.com)
 *
 * Copyright 2007 Novell, Inc. (http://www.novell.com)
 *
 * See the LICENSE file included with the distribution for details.
 * 
 */


#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>

#include "downloader.h"
#include "file-downloader.h"
#include "mms-downloader.h"
#include "runtime.h"
#include "utils.h"
#include "error.h"
#include "debug.h"

//
// Downloader
//

downloader_create_state_func Downloader::create_state = NULL;
downloader_destroy_state_func Downloader::destroy_state = NULL;
downloader_open_func Downloader::open_func = NULL;
downloader_send_func Downloader::send_func = NULL;
downloader_abort_func Downloader::abort_func = NULL;
downloader_header_func Downloader::header_func = NULL;
downloader_body_func Downloader::body_func = NULL;
downloader_create_webrequest_func Downloader::request_func = NULL;

Downloader::Downloader ()
{
	LOG_DOWNLOADER ("Downloader::Downloader ()\n");
	
	SetObjectType (Type::DOWNLOADER);

	downloader_state = Downloader::create_state (this);
	consumer_closure = NULL;
	context = NULL;
	streaming_features = HttpStreamingFeaturesNone;
	notify_size = NULL;
	this->write = NULL;
	internal_dl = NULL;
	
	send_queued = false;
	started = false;
	aborted = false;
	completed = false;
	file_size = -2;
	total = 0;
	
	filename = NULL;
	buffer = NULL;
	failed_msg = NULL;
}


Downloader::~Downloader ()
{
	LOG_DOWNLOADER ("Downloader::~Downloader ()\n");
	
	Downloader::destroy_state (downloader_state);
	
	g_free (filename);
	g_free (buffer);
	g_free (failed_msg);

	if (internal_dl != NULL)
		delete internal_dl;
}

void
Downloader::InternalAbort ()
{
	LOG_DOWNLOADER ("Downloader::InternalAbort ()\n");
	if (!GetSurface ())
		return;

	abort_func (downloader_state);
}

void
Downloader::Abort ()
{
	LOG_DOWNLOADER ("Downloader::Abort ()\n");
	
	SetCurrentDeployment ();
	
	if (!aborted && !failed_msg) {
		InternalAbort ();
		SetDownloadProgress (0.0);
		send_queued = false;
		aborted = true;
	}
}

char *
Downloader::GetDownloadedFilename (const char *partname)
{
	LOG_DOWNLOADER ("Downloader::GetDownloadedFilename (%s)\n", filename);
	
	return internal_dl->GetDownloadedFilename (partname);
}

char *
Downloader::GetResponseText (const char *PartName, guint64 *size)
{
	LOG_DOWNLOADER ("Downloader::GetResponseText (%s, %p)\n", PartName, size);

	// This is a horrible hack to work around mozilla bug #444160
	// Basically if a very small file is downloaded (<64KB in mozilla as of Jan5/09
	// it can be inserted into a shared cache map, and served up to us without ever
	// giving us the filename for a NP_ASFILE request.
	if (PartName == NULL && buffer != NULL) {
		char *data;
		char b[4096];
		ssize_t nread;
		GByteArray *buf;

		TextStream *stream = new TextStream ();

		if (!stream->OpenBuffer (buffer, total)) {
			delete stream;
			return NULL;
		}

		buf = g_byte_array_new ();
		while ((nread = stream->Read (b, sizeof (b))) > 0)
			g_byte_array_append (buf, (const guint8 *) b, nread);

		*size = buf->len;

		g_byte_array_append (buf, (const guint8 *) "", 1);
		data = (char *) buf->data;

		g_byte_array_free (buf, false);
		delete stream;

		return data;
	}

	return internal_dl->GetResponseText (PartName, size);
}

void
Downloader::InternalOpen (const char *verb, const char *uri, bool streaming)
{
	LOG_DOWNLOADER ("Downloader::InternalOpen (%s, %s, %i)\n", verb, uri, streaming);

	open_func (verb, uri, streaming, downloader_state);
}

static bool
scheme_is (const Uri *uri, const char *scheme)
{
	return uri->protocol && !strcmp (uri->protocol, scheme);
}

static bool
same_scheme (const Uri *uri1, const Uri *uri2)
{
	return uri1->protocol && uri2->protocol &&
		!strcmp (uri1->protocol, uri2->protocol);
}

static bool
same_domain (const Uri *uri1, const Uri *uri2)
{
	return (g_ascii_strcasecmp (uri1->host, uri2->host) == 0);
}

static bool
check_redirection_policy (const char *uri, const char *final_uri, DownloaderAccessPolicy policy)
{
	if (!uri || !final_uri)
		return true;

	Uri *orig = new Uri ();
	orig->Parse (uri);

	Uri *final = new Uri ();
	final->Parse (final_uri);

	bool retval = true;
	switch(policy) {
	case DownloaderPolicy:
	case XamlPolicy:
	case StreamingPolicy: //Streaming media
		//Redirection allowed: same domain.
		if (g_ascii_strcasecmp (uri, final_uri) == 0)
			break;
		if (!same_domain (orig, final))
			retval = false;
		break;
	case MediaPolicy:
		if (g_ascii_strcasecmp (uri, final_uri) != 0)
			retval = false;
		break;
	default:
		break;
	}
	delete orig;
	delete final;
	return retval;
}

static bool
validate_policy (const char *location, const char *uri, DownloaderAccessPolicy policy)
{
	if (!location || !uri)
		return true;
	
	Uri *source = new Uri ();
	source->Parse (uri);
	if (source->host == NULL) {
		//relative uri, not checking policy
		delete source;
		return true;
	}

	Uri *target = new Uri ();
	target->Parse (location);

	bool retval = true;
	switch (policy) {
	case DownloaderPolicy:
		//Allowed schemes: http, https
		if (!scheme_is (target, "http") && !scheme_is (target, "https"))
			retval = false;
		//X-Scheme: no
		if (!same_scheme (target, source))
			retval = false;
		//X-Domain: no
		if (!same_domain (target, source))
			retval = false;
		break;
	case MediaPolicy: //Media, images, ASX
		//Allowed schemes: http, https, file
		if (!scheme_is (target, "http") && !scheme_is (target, "https") && !scheme_is (target, "file"))
			retval = false;
		//X-Scheme: no
		if (!same_scheme (target, source))
			retval = false;
		//X-Domain: if not https
		if (scheme_is (source, "https") && !same_domain (target, source))
			retval = false;
		break;
	case XamlPolicy: //XAML files, font files
		//Allowed schemes: http, https, file
		if (!scheme_is (target, "http") && !scheme_is (target, "https") && !scheme_is (target, "file"))
			retval = false;
		//X-Scheme: no
		if (!same_scheme (target, source))
			retval =false;
		//X-domain: no
		if (!same_domain (target, source))
			retval = false;
		break;
	case StreamingPolicy: //Streaming media
		//Allowed schemes: http
		if (!scheme_is (target, "http"))
			retval = false;
		//X-scheme: Not from https
		if (scheme_is (source, "https") && !same_scheme (source, target))
			retval = false;
	default:
		break;
	}
	delete source;
	delete target;
	return retval;
}

void
Downloader::Open (const char *verb, const char *uri, DownloaderAccessPolicy policy)
{
	LOG_DOWNLOADER ("Downloader::Open (%s, %s)\n", verb, uri);
	
	send_queued = false;
	started = false;
	aborted = false;
	completed = false;
	file_size = -2;
	total = 0;
	access_policy = policy;

	g_free (failed_msg);
	g_free (filename);
	g_free (buffer);
	failed_msg = NULL;
	filename = NULL;
	buffer = NULL;

	//FIXME: ONLY VALIDATE IF USED FROM THE PLUGIN
	char *location = g_strdup (GetSurface()->GetSourceLocation ());
	if (!validate_policy (location, uri, policy)) {
		LOG_DOWNLOADER ("aborting due to security policy violation\n");
		failed_msg = g_strdup ("Security Policy Violation");
		Abort ();
		g_free (location);
		return;
	}
	g_free (location);

	if (strncmp (uri, "mms://", 6) == 0) {
		internal_dl = (InternalDownloader *) new MmsDownloader (this);
	} else {
		internal_dl = (InternalDownloader *) new FileDownloader (this);
	}

	send_queued = false;

	SetUri (uri);

	internal_dl->Open (verb, uri);
}

void
Downloader::InternalSetHeader (const char *header, const char *value)
{
	LOG_DOWNLOADER ("Downloader::InternalSetHeader (%s, %s)\n", header, value);
	
	header_func (downloader_state, header, value);
}

void
Downloader::InternalSetBody (void *body, guint32 length)
{
	LOG_DOWNLOADER ("Downloader::InternalSetBody (%p, %u)\n", body, length);
	
	body_func (downloader_state, body, length);
}

void
Downloader::SendInternal ()
{
	LOG_DOWNLOADER ("Downloader::SendInternal ()\n");
	
	if (!GetSurface ()) {
		// The plugin is already checking for surface before calling Send, so
		// if we get here, it's either managed code doing something wrong or ourselves.
		g_warning ("Downloader::SendInternal (): No surface!\n");
	}

	if (!send_queued)
		return;
	
	send_queued = false;
	
	if (completed) {
		// Consumer is re-sending a request which finished successfully.
		NotifyFinished (NULL);
		return;
	}
	
	if (failed_msg != NULL) {
		// Consumer is re-sending a request which failed.
		Emit (DownloadFailedEvent, new ErrorEventArgs (DownloadError, 1, failed_msg));
		return;
	}
	
	started = true;
	aborted = false;
	
	send_func (downloader_state);
}

static void
send_async (EventObject *user_data)
{
	Downloader *downloader = (Downloader *) user_data;
	
	downloader->SendInternal ();
}

void
Downloader::Send ()
{
	LOG_DOWNLOADER ("Downloader::Send ()\n");
	
	if (!GetSurface ()) {
		// The plugin is already checking for surface before calling Send, so
		// if we get here, it's either managed code doing something wrong or ourselves.
		g_warning ("Downloader::Send (): No surface!\n");
	}

	if (send_queued)
		return;
	
	send_queued = true;
	SetStatusText ("");
	SetStatus (0);
	
	AddTickCall (send_async);
}

void
Downloader::SendNow ()
{
	LOG_DOWNLOADER ("Downloader::SendNow ()\n");
	
	send_queued = true;
	SetStatusText ("");
	SetStatus (0);
	
	SendInternal ();
}

//
// A zero write means that we are done
//
void
Downloader::Write (void *buf, gint32 offset, gint32 n)
{
	LOG_DOWNLOADER ("Downloader::Write (%p, %i, %i). Uri: %s\n", buf, offset, n, GetUri ());
	
	SetCurrentDeployment ();
	
	if (aborted)
		return;
		
	if (!GetSurface ())
		return;
	
	internal_dl->Write (buf, offset, n);
}

void
Downloader::InternalWrite (void *buf, gint32 offset, gint32 n)
{
	LOG_DOWNLOADER ("Downloader::InternalWrite (%p, %i, %i)\n", buf, offset, n);
	
	double progress;

	// Update progress
	if (n > 0)
		total += n;

	if (file_size >= 0) {
		if ((progress = total / (double) file_size) > 1.0)
			progress = 1.0;
	} else 
		progress = 0.0;

	SetDownloadProgress (progress);
	
	Emit (DownloadProgressChangedEvent);

	if (write)
		write (buf, offset, n, consumer_closure);

	// This is a horrible hack to work around mozilla bug #444160
	// See Downloader::GetResponseText for an explanation
	if (n == total && total < 65536) {
		buffer = (char *) g_malloc (total);
		memcpy (buffer, buf, total);
	} 
}

void
Downloader::SetFilename (const char *fname)
{
	LOG_DOWNLOADER ("Downloader::SetFilename (%s)\n", fname);
	
	if (buffer) {
		g_free (buffer);
		buffer = NULL;
	}

	if (filename)
		g_free (filename);

	filename = g_strdup (fname);
	((FileDownloader *)internal_dl)->setFilename (filename);
}

void
Downloader::NotifyFinished (const char* final_uri)
{
	if (aborted)
		return;
	
	SetCurrentDeployment ();
	
	if (!GetSurface ())
		return;

	if (!check_redirection_policy (GetUri (), final_uri, access_policy)) {
		LOG_DOWNLOADER ("aborting due to security policy violation\n");
		failed_msg = g_strdup ("Security Policy Violation");
		Abort ();
		return;
	}

	SetDownloadProgress (1.0);
	
	Emit (DownloadProgressChangedEvent);
	
	// HACK, we should provide the actual status text and code
	SetStatusText ("OK");
	SetStatus (200);
	
	completed = true;

	Emit (CompletedEvent, NULL);
}

void
Downloader::NotifyFailed (const char *msg)
{
	LOG_DOWNLOADER ("Downloader::NotifyFailed (%s)\n", msg);
	
	/* if we've already been notified of failure, no-op */
	if (failed_msg)
		return;
	
	SetCurrentDeployment ();
	
	if (!GetSurface ())
		return;
	
	// SetStatus (400);
	// For some reason the status is 0, not updated on errors?
	
	Emit (DownloadFailedEvent, new ErrorEventArgs (DownloadError, 1, msg));
	
	// save the error in case someone else calls ::Send() on this
	// downloader for the same uri.
	failed_msg = g_strdup (msg);
}

void
Downloader::NotifySize (gint64 size)
{
	LOG_DOWNLOADER ("Downloader::NotifySize (%lld)\n", size);
	
	file_size = size;
	
	if (aborted)
		return;
	
	SetCurrentDeployment ();
	
	if (!GetSurface ())
		return;
	
	if (notify_size)
		notify_size (size, consumer_closure);
}

bool
Downloader::Started ()
{
	LOG_DOWNLOADER ("Downloader::Started (): %i\n", started);
	
	return started;
}

bool
Downloader::Completed ()
{
	LOG_DOWNLOADER ("Downloader::Completed (), filename: %s\n", filename);
	
	return completed;
}

void
Downloader::SetWriteFunc (downloader_write_func write,
			  downloader_notify_size_func notify_size,
			  gpointer data)
{
	LOG_DOWNLOADER ("Downloader::SetWriteFunc\n");
	
	this->write = write;
	this->notify_size = notify_size;
	this->consumer_closure = data;
}

void
Downloader::SetFunctions (downloader_create_state_func create_state,
			  downloader_destroy_state_func destroy_state,
			  downloader_open_func open,
			  downloader_send_func send,
			  downloader_abort_func abort,
			  downloader_header_func header,
			  downloader_body_func body,
			  downloader_create_webrequest_func request,
			  bool only_if_not_set)
{
	LOG_DOWNLOADER ("Downloader::SetFunctions\n");
	
	if (only_if_not_set &&
	    (Downloader::create_state != NULL ||
	     Downloader::destroy_state != NULL ||
	     Downloader::open_func != NULL ||
	     Downloader::send_func != NULL ||
	     Downloader::abort_func != NULL ||
	     Downloader::header_func != NULL ||
	     Downloader::body_func != NULL ||
	     Downloader::request_func != NULL))
	  return;

	Downloader::create_state = create_state;
	Downloader::destroy_state = destroy_state;
	Downloader::open_func = open;
	Downloader::send_func = send;
	Downloader::abort_func = abort;
	Downloader::header_func = header;
	Downloader::body_func = body;
	Downloader::request_func = request;
}

void
Downloader::SetDownloadProgress (double progress)
{
	LOG_DOWNLOADER ("Downloader::SetDownloadProgress\n");
	
	SetValue (Downloader::DownloadProgressProperty, Value (progress));
}

double
Downloader::GetDownloadProgress ()
{
	LOG_DOWNLOADER ("Downloader::GetDownloadProgress\n");
	
	return GetValue (Downloader::DownloadProgressProperty)->AsDouble ();
}

void
Downloader::SetStatusText (const char *text)
{
	LOG_DOWNLOADER ("Downloader::SetStatusText\n");
	
	SetValue (Downloader::StatusTextProperty, Value (text));
}

const char *
Downloader::GetStatusText ()
{
	LOG_DOWNLOADER ("Downloader::GetStatusText\n");
	
	Value *value = GetValue (Downloader::StatusTextProperty);
	
	return value ? value->AsString () : NULL;
}

void
Downloader::SetStatus (int status)
{
	LOG_DOWNLOADER ("Downloader::SetStatus\n");
	
	SetValue (Downloader::StatusProperty, Value (status));
}

int
Downloader::GetStatus ()
{
	LOG_DOWNLOADER ("Downloader::GetStatus\n");
	
	return GetValue (Downloader::StatusProperty)->AsInt32 ();
}

void
Downloader::SetUri (const char *uri)
{
	LOG_DOWNLOADER ("Downloader::SetUri (%s)\n", uri);
	
	SetValue (Downloader::UriProperty, Value (uri));

}

const char *
Downloader::GetUri ()
{
	LOG_DOWNLOADER ("Downloader::GetUri ()\n");
	
	Value *value = GetValue (Downloader::UriProperty);
	
	return value ? value->AsString () : NULL;
}

double
downloader_get_download_progress (Downloader *dl)
{
	return dl->GetDownloadProgress ();
}

const char *
downloader_get_status_text (Downloader *dl)
{
	return dl->GetStatusText ();
}

int
downloader_get_status (Downloader *dl)
{
	return dl->GetStatus ();
}

void
downloader_set_uri (Downloader *dl, const char *uri)
{
	dl->SetUri (uri);
}

const char *
downloader_get_uri (Downloader *dl)
{
	return dl->GetUri ();
}

void
downloader_abort (Downloader *dl)
{
	dl->Abort ();
}

char *
downloader_get_response_text (Downloader *dl, const char *PartName, guint64 *size)
{
	return dl->GetResponseText (PartName, size);
}

//void
//downloader_open (Downloader *dl, const char *verb, const char *uri)
//{
//	dl->Open (verb, uri);
//}

void
downloader_send (Downloader *dl)
{
	if (!dl->Completed () && dl->Started ())
		downloader_abort (dl);
	
	dl->Send ();
}

void
downloader_set_functions (downloader_create_state_func create_state,
			  downloader_destroy_state_func destroy_state,
			  downloader_open_func open,
			  downloader_send_func send,
			  downloader_abort_func abort, 
			  downloader_header_func header,
			  downloader_body_func body,
			  downloader_create_webrequest_func request)
{
	Downloader::SetFunctions (create_state, destroy_state,
				  open, send, abort, header, body, request, false);
}

/*
 * DownloaderRequest / DownloaderResponse
 */

DownloaderResponse::~DownloaderResponse ()
{
	if (request != NULL && request->GetDownloaderResponse () == this)
		request->SetDownloaderResponse (NULL);
	GetDeployment ()->UnregisterDownloader (this);
}

DownloaderResponse::DownloaderResponse ()
{
	aborted = false;
	started = NULL;
	available = NULL;
	finished = NULL;
	context = NULL;
	request = NULL;
	SetDeployment (Deployment::GetCurrent ());
	GetDeployment ()->RegisterDownloader (this);
}

DownloaderResponse::DownloaderResponse (DownloaderResponseStartedHandler started, DownloaderResponseDataAvailableHandler available, DownloaderResponseFinishedHandler finished, gpointer context)
{
	this->aborted = false;
	this->started = started;
	this->available = available;
	this->finished = finished;
	this->context = context;
	this->request = NULL;
	SetDeployment (Deployment::GetCurrent ());
	GetDeployment ()->RegisterDownloader (this);
}

DownloaderRequest::DownloaderRequest (const char *method, const char *uri)
{
	this->method = g_strdup (method);
	this->uri = g_strdup (uri);
	this->response = NULL;
	SetDeployment (Deployment::GetCurrent ());
	GetDeployment ()->RegisterDownloader (this);
}

DownloaderRequest::~DownloaderRequest ()
{
	g_free (method);
	g_free (uri);
	if (response != NULL && response->GetDownloaderRequest () == this)
		response->SetDownloaderRequest (NULL);
	GetDeployment ()->UnregisterDownloader (this);
}

void
*downloader_create_webrequest (Downloader *dl, const char *method, const char *uri)
{
	return dl->GetRequestFunc() (method, uri, dl->GetContext());
}

bool
downloader_request_get_response (DownloaderRequest *dr, DownloaderResponseStartedHandler started, DownloaderResponseDataAvailableHandler available, DownloaderResponseFinishedHandler finished, gpointer context)
{
	return dr->GetResponse (started, available, finished, context);
}

void downloader_response_set_header_visitor (DownloaderResponse *dr, DownloaderResponseHeaderVisitorCallback visitor)
{
	dr->SetHeaderVisitor (visitor);
}

bool
downloader_request_is_aborted (DownloaderRequest *dr)
{
	return dr->IsAborted ();
}

void
downloader_request_set_http_header (DownloaderRequest *dr, const char *name, const char *value)
{
	dr->SetHttpHeader (name, value);
}

void
downloader_request_set_body (DownloaderRequest *dr, void *body, int size)
{
	dr->SetBody (body, size);
}

void
downloader_write (Downloader *dl, void *buf, gint32 offset, gint32 n)
{
	dl->Write (buf, offset, n);
}

void
downloader_notify_finished (Downloader *dl, const char *fname)
{
	dl->SetFilename (fname);
	dl->NotifyFinished (NULL);
}

void
downloader_notify_error (Downloader *dl, const char *msg)
{
	dl->NotifyFailed (msg);
}

void
downloader_notify_size (Downloader *dl, gint64 size)
{
	dl->NotifySize (size);
}


static gpointer
dummy_downloader_create_state (Downloader* dl)
{
	g_warning ("downloader_set_function has never been called.\n");
	return NULL;
}

static void
dummy_downloader_destroy_state (gpointer state)
{
	g_warning ("downloader_set_function has never been called.\n");
}

static void
dummy_downloader_open (const char *verb, const char *uri, bool open, gpointer state)
{
	g_warning ("downloader_set_function has never been called.\n");
}

static void
dummy_downloader_send (gpointer state)
{
	g_warning ("downloader_set_function has never been called.\n");
}

static void
dummy_downloader_abort (gpointer state)
{
	g_warning ("downloader_set_function has never been called.\n");
}

static void
dummy_downloader_header (gpointer state, const char *header, const char *value)
{
	g_warning ("downloader_set_function has never been called.\n");
}

static void
dummy_downloader_body (gpointer state, void *body, guint32 length)
{
	g_warning ("downloader_set_function has never been called.\n");
}

static gpointer
dummy_downloader_create_web_request (const char *method, const char *uri, gpointer context)
{
	g_warning ("downloader_set_function has never been called.\n");
	return NULL;
}


void
downloader_init (void)
{	
	Downloader::SetFunctions (dummy_downloader_create_state,
				  dummy_downloader_destroy_state,
				  dummy_downloader_open,
				  dummy_downloader_send,
				  dummy_downloader_abort,
				  dummy_downloader_header,
				  dummy_downloader_body,
				  dummy_downloader_create_web_request, true);
}
