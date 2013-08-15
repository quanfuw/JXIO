#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <map>
#include <jni.h>

#include "cJXBridge.h"


static jclass cls;
static JavaVM *cached_jvm;
static jmethodID jmethodID_on_event; // handle to java cb method



struct bufferEventQ{
	char* buf;
	int offset;
	void* evLoop;
	int eventsNum;
};

// globals
char* buf;
xio_mr* mr;
std::map<void*,bufferEventQ*>* mapContextEventQ;

/* server private data */
struct hw_server_data {
		struct xio_msg  rsp;	/* global message */
};

// JNI inner functions implementations

extern "C" JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM *jvm, void* reserved)
{
	printf("in cJXBridge - JNI_OnLoad\n");

	cached_jvm = jvm;
	JNIEnv *env;
	if ( jvm->GetEnv((void **)&env, JNI_VERSION_1_4)) { //direct buffer requires java 1.4
		return JNI_ERR; /* JNI version not supported */
	}

	cls = env->FindClass("com/mellanox/JXBridge");
	if (cls == NULL) {
		fprintf(stderr, "in cJXBridge - java class was NOT found\n");
		return JNI_ERR;
	}

	jmethodID_on_event = env->GetStaticMethodID(cls, "on_event", "()V");
	if (jmethodID_on_event == NULL) {
		fprintf(stderr,"in cJXBridge - on_event() callback method was NOT found\n");
		return JNI_ERR;
	}

	//Katya - some init code
	mapContextEventQ = new std::map<void*,bufferEventQ*> ();
	if(mapContextEventQ== NULL){
		fprintf(stderr, "Error, Could not allocate memory ");
		return NULL;
	}

	printf("in cJXBridge -  java callback methods were found and cached\n");
	return JNI_VERSION_1_4;  //direct buffer requires java 1.4
}



extern "C" JNIEXPORT void JNICALL JNI_OnUnload(JavaVM *jvm, void* reserved)
{
	// NOTE: We never reached this place
	return;
}


// callback back to JAVA
int invoke_on_event_callback(){
	JNIEnv *jniEnv;
	if (cached_jvm->GetEnv((void **)&jniEnv, JNI_VERSION_1_4)) {
		printf("Error getting JNIEnv In C++ method");
		return 1;
	}
	printf("before jniEnv->CallStaticVoidMethod... on_event\n");
	jniEnv->CallStaticVoidMethod(cls, jmethodID_on_event);
	printf("after jniEnv->CallStaticVoidMethod...on_event\n");
	return 0;
}



// implementation of the XIO callbacks

int on_new_session_callback(struct xio_session *session,
		struct xio_new_session_req *req,
		void *cb_prv_data)
{

	struct xio_context * ctx;
	struct bufferEventQ* beq;
	std::map<void*,bufferEventQ*>::iterator it;
	int32_t event;
	char * ip;
	intptr_t ptrSession;


	// here we will build and enter the new event to the event queue
	printf("on_new_session_callback. \n");


	ctx = (xio_context*)cb_prv_data;
	it = mapContextEventQ->find(ctx);
	if (it == mapContextEventQ->end()){
			printf ("error! no entry for this ctx\n");
			return 1;
		}


	event = htonl (4);
	beq = it->second;
	memcpy(beq->buf + beq->offset, &event, sizeof(event));//TODO: to make number of event enum
	beq->offset += sizeof(event); //TODO: static variable??? pass it from java


	void* p1 =  session;
	int64_t t = htobe64(intptr_t(p1));
//	ptrSession = ((intptr_t) session);
	memset (beq->buf + beq->offset,0, 8 );
	memcpy(beq->buf + beq->offset, &t, sizeof(ptrSession));
//	memcpy(beq->buf + beq->offset, session, sizeof(session));
//	printf ("**ptr is %ld. size is %d, size 2 is %ld, sizeof long is \n", ptrSession, sizeof(ptrSession), t, sizeof(long));
//	printf ("** ptr is %p, located in %p\n", p1, &p1);
//	printf ("** ptr is %dl, located in %p\n", t, &t);

	beq->offset += sizeof(int64_t);


	int32_t lenUri = htonl(req->uri_len);

	memcpy(beq->buf + beq->offset, &lenUri, sizeof(int32_t));
	beq->offset += sizeof(int32_t);

	strcpy(beq->buf + beq->offset,req->uri);

	beq->offset += req->uri_len ;

	int len;
	int32_t ipLen;
	struct sockaddr *ipStruct = (struct sockaddr *)&req->src_addr;


	if (ipStruct->sa_family == AF_INET) {
			static char addr[INET_ADDRSTRLEN];
			struct sockaddr_in *v4 = (struct sockaddr_in *)ipStruct;
			ip = (char *)inet_ntop(AF_INET, &(v4->sin_addr),
						 addr, INET_ADDRSTRLEN);
			len = INET_ADDRSTRLEN;


	}else if (ipStruct->sa_family == AF_INET6) {
		static char addr[INET6_ADDRSTRLEN];
		struct sockaddr_in6 *v6 = (struct sockaddr_in6 *)ipStruct;
		ip = (char *)inet_ntop(AF_INET6, &(v6->sin6_addr),
					 addr, INET6_ADDRSTRLEN);
		len = INET6_ADDRSTRLEN;

	}else{
		fprintf(stderr, "can not get src ip");
		len = 0;

	}
	ipLen = htonl (len);


	memcpy(beq->buf + beq->offset, &ipLen, sizeof(int32_t));
	beq->offset += sizeof(int32_t);
	strcpy(beq->buf + beq->offset,ip);

	beq->offset += len ;

	//need to stop the event queue only if this is the first callback
	if (!beq->eventsNum){
			printf("inside on_new_session_callback - stopping the event queue\n");
			xio_ev_loop_stop(beq->evLoop);
	}
	beq->eventsNum++;



	printf("the end of new session callback\n");

	return 0;
}

extern "C" JNIEXPORT jlongArray JNICALL Java_com_mellanox_JXBridge_createEQHNative(JNIEnv *env, jclass cls)
{
	void * ev_loop;
	struct xio_context	*ctx;
	jlongArray dataToJava;
	jlong temp[2];

	ev_loop = xio_ev_loop_init();
	if (ev_loop == NULL){
		fprintf(stderr, "Error, ev_loop_init failed");
		return 0;
	}

	ctx = xio_ctx_open(NULL, ev_loop);
	if (ctx == NULL){
		fprintf(stderr, "Error, ev_loop_init failed");
		return 0;
	}

	dataToJava = env->NewLongArray(2);
	 if (dataToJava == NULL) {
		 printf("Error in allocating array via jni\n");
		 return NULL;
	 }
	 // fill a temp structure to use to populate the java long array

	 temp[0] = (jlong)(intptr_t) ev_loop;
	 temp[1] = (jlong)(intptr_t) ctx;

	 // move from the temp structure to the java structure
	 env->SetLongArrayRegion(dataToJava,0, 2, temp);
	 printf("createEQHNative done\n");
	 return dataToJava;

}


//Katya
extern "C" JNIEXPORT jobject JNICALL Java_com_mellanox_JXBridge_allocateEventQNative(JNIEnv *env, jclass cls, jlong ptrCtx, jlong ptrEvLoop, jint eventQSize)
{

	struct xio_context *ctx;
	struct bufferEventQ* beq = NULL;
	int total_size;

	total_size = eventQSize;

	//allocating struct for event queue
	beq = (bufferEventQ*)malloc(sizeof(bufferEventQ));

	if (beq == NULL){
		fprintf(stderr, "Error, Could not allocate memory ");
		return NULL;
	}

	//allocating buffer that will hold the event queue
	beq->buf = (char*)malloc(total_size * sizeof(char));
	if (beq->buf== NULL){
		fprintf(stderr, "Error, Could not allocate memory for Event Queue buffer");
		return NULL;
	}

	beq->offset = 0;
	beq->evLoop = (void *)ptrEvLoop;
	beq->eventsNum = 0;

	ctx = (struct xio_context *)ptrCtx;
	//inserting into map


	mapContextEventQ->insert(std::pair<void*, bufferEventQ*>(ctx, beq));

	jobject jbuf = env->NewDirectByteBuffer(beq->buf, total_size );
	printf("allocateEventQNative done\n");

	return jbuf;
}



//Katya
extern "C" JNIEXPORT void JNICALL Java_com_mellanox_JXBridge_closeEQHNative(JNIEnv *env, jclass cls, jlong ptrCtx, jlong ptrEvLoop)
{
	void* ev_loop;
	struct xio_context *ctx;
	struct bufferEventQ* beq;
	std::map<void*,bufferEventQ*>::iterator it;

	ctx = (struct xio_context *)ptrCtx;

	printf("beginning of closeEQH\n");
	it = mapContextEventQ->find(ctx);
	if (it == mapContextEventQ->end()){
		printf ("error! no entry for this ctx\n");
	}else{
		beq = it->second;
		//delete from map
		mapContextEventQ->erase(it);
		//free memory
		free(beq->buf);
		free(beq);
	}
	xio_ctx_close(ctx);

	ev_loop = (void*) ptrEvLoop;
	/* destroy the event loop */
	xio_ev_loop_destroy(&ev_loop);
	printf("end of closeEQH\n");
}



//Katya
extern "C" JNIEXPORT jint JNICALL Java_com_mellanox_JXBridge_runEventLoopNative(JNIEnv *env, jclass cls, jlong ptrCtx)
{

	void *evLoop;
	struct xio_context *ctx;
	std::map<void*,bufferEventQ*>::iterator it;
	struct bufferEventQ* beq;

	ctx = (struct xio_context *)ptrCtx;

	printf("before xio_ev_loop_run. ctx is %p\n", ctx);

	it = mapContextEventQ->find(ctx);
	if (it == mapContextEventQ->end()){
			printf ("error! no entry for this ctx\n");
			return 1;
		}
	beq = it->second;

    //update offset to 0: for indication if this is the first callback called
	beq->offset = 0;
	beq->eventsNum = 0;

	xio_ev_loop_run(beq->evLoop);

	printf("after xio_ev_loop_run\n");
	return 0;

}

//Katya
extern "C" JNIEXPORT jint JNICALL Java_com_mellanox_JXBridge_getNumEventsQNative(JNIEnv *env, jclass cls, jlong ptrCtx)
{
	int ret_val;
	void *evLoop;
	struct xio_context *ctx;
	std::map<void*,bufferEventQ*>::iterator it;
	struct bufferEventQ* beq;
	int eventsNum;

//	printf("getNumEventsQ\n");

	ctx = (struct xio_context *)ptrCtx;

	it = mapContextEventQ->find(ctx);
	if (it == mapContextEventQ->end()){
		printf ("error! no entry for this ctx\n");
		return 0;
	}
	beq = it->second;

	eventsNum = beq->eventsNum;

	return eventsNum;

}

//Katya
extern "C" JNIEXPORT jboolean JNICALL Java_com_mellanox_JXBridge_closeSessionClientNative(JNIEnv *env, jclass cls, jlong ptrSes)
{

	int ret_val;
	struct xio_session *session;

	session = (struct xio_session *)ptrSes;
	ret_val = xio_session_close (session);
	if (ret_val){
		fprintf(stderr, "Error, xio_session_close failed");
		return false;
	}

	printf("end of closeSessionClient\n");
	return true;

}

//Katya
extern "C" JNIEXPORT jboolean JNICALL Java_com_mellanox_JXBridge_closeConnectionClientNative(JNIEnv *env, jclass cls, jlong ptrCon)
{

	int ret_val;
	struct xio_connection *con;
	struct xio_session *session;


	con = (struct xio_connection *)ptrCon;
	ret_val = xio_disconnect (con);

	if (ret_val){
		fprintf(stderr, "Error, xio_disconnect failed");
		return false;
	}

	printf("end of closeConnectionClient\n");
	return true;

}




//Katya
extern "C" JNIEXPORT jboolean JNICALL Java_com_mellanox_JXBridge_stopServerNative(JNIEnv *env, jclass cls, jlong ptrServer)
{

	int ret_val;
	struct xio_server *server;

	server = (struct xio_server *)ptrServer;
	ret_val = xio_unbind (server);

	if (ret_val){
		fprintf(stderr, "Error, xio_unbind failed");
		return false;
	}

	return true;

}


int on_session_redirected_callback(struct xio_session *session,
		struct xio_new_session_rsp *rsp,
		void *cb_prv_data)
{
	// here we will build and enter the new event to the event queue
	
	// and after calling the callback to the JAVA
	if(invoke_on_event_callback()){
		printf("Error invoking the callback to JAVA");
		return 1;
		}
		
	return 0;
}



int on_msg_send_complete_callback(struct xio_session *session,
		struct xio_msg *msg,
		void *cb_prv_data)
{
	// here we will build and enter the new event to the event queue
	
	// and after calling the callback to the JAVA
	if(invoke_on_event_callback()){
		printf("Error invoking the callback to JAVA");
		return 1;
		}
		
	return 0;
}



int on_msg_hdr_avail_callback(struct xio_session *session,
		struct xio_msg *msg,
		void *cb_prv_data)
{
	// here we will build and enter the new event to the event queue
	
	// and after calling the callback to the JAVA
	if(invoke_on_event_callback()){
		printf("Error invoking the callback to JAVA");
		return 1;
		}
		
	return 0;
}


int on_msg_callback(struct xio_session *session,
		struct xio_msg *msg,
		int more_in_batch,
		void *cb_prv_data)
{

	struct xio_context * ctx;
	struct bufferEventQ* beq;
	std::map<void*,bufferEventQ*>::iterator it;
	int32_t event;


	// here we will build and enter the new event to the event queue
	printf("on_msg_callback\n");
	ctx = (xio_context*)cb_prv_data;
	it = mapContextEventQ->find(ctx);
	if (it == mapContextEventQ->end()){
		printf ("error! no entry for this ctx\n");
		return 1;
	}


	event = htonl (3);
	beq = it->second;

	memcpy(beq->buf + beq->offset, &event, sizeof(event));//TODO: to make number of event enum
	beq->offset += sizeof(event); //TODO: static variable??? pass it from java
		
	//need to stop the event queue only if this is the first callback
	if (!beq->eventsNum){
			printf("inside on_msg_callback - stopping the event queue\n");
			xio_ev_loop_stop(beq->evLoop);
	}
	beq->eventsNum++;

	return 0;
}


int on_msg_error_callback(struct xio_session *session,
            enum xio_status error,
            struct xio_msg  *msg,
            void *conn_user_context)
{
	// here we will build and enter the new event to the event queue
	
	// and after calling the callback to the JAVA
	if(invoke_on_event_callback()){
		printf("Error invoking the callback to JAVA");
		return 1;
		}
		
	return 0;
}



//Katya
int on_session_established_callback(struct xio_session *session,
		struct xio_new_session_rsp *rsp,
		void *cb_prv_data){

	struct xio_context * ctx;
	struct bufferEventQ* beq;
	std::map<void*,bufferEventQ*>::iterator it;
	int32_t event;

	printf("got on_session_established_callback\n");
	ctx = (xio_context*)cb_prv_data;
	it = mapContextEventQ->find(ctx);
	if (it == mapContextEventQ->end()){
			printf ("error! no entry for this ctx\n");
			return 1;
		}


	event = htonl (2);
	beq = it->second;

	memcpy(beq->buf + beq->offset, &event, sizeof(event));//TODO: to make number of event enum
	beq->offset += sizeof(event);

	//need to stop the event queue only if this is the first callback
	if (!beq->eventsNum){
		printf("inside on_session_established_callback - stopping the event queue\n");
		xio_ev_loop_stop(beq->evLoop);
	}

	beq->eventsNum++;

	return 0;
}

int on_session_event_callback(struct xio_session *session,
		struct xio_session_event_data *event_data,
		void *cb_prv_data){


	struct xio_context * ctx;
	struct bufferEventQ* beq;
	std::map<void*,bufferEventQ*>::iterator it;
	int32_t event, error_type, error_reason;
	
	printf("the beginning of on_session_event_callback\n");
	ctx = (xio_context*)cb_prv_data;
	it = mapContextEventQ->find(ctx);
	if (it == mapContextEventQ->end()){
			printf ("error! no entry for this ctx\n");
			return 1;
		}


	beq = it->second;

	event = htonl (0);
	error_type = htonl(event_data->event);
	error_reason = htonl (event_data->reason);

	memcpy(beq->buf + beq->offset, &event, sizeof(event));
	beq->offset +=sizeof(event);
	memcpy(beq->buf + beq->offset, &error_type, sizeof(error_type));
	beq->offset +=sizeof(error_type);
	memcpy(beq->buf + beq->offset, &error_reason, sizeof(error_reason));
	beq->offset +=sizeof(error_reason);

	//need to stop the event queue only if this is the first callback
	if (!beq->eventsNum){
		printf("inside on_session_event_callback - stopping the event queue\n");
		xio_ev_loop_stop(beq->evLoop);
	}
	beq->eventsNum++;


	printf("the end of on_session_event_callback\n");
	return 0;

}

// implementation of the native method
//
extern "C" JNIEXPORT jobject JNICALL Java_com_mellanox_JXBridge_createMsgPooNative(JNIEnv *env, jclass cls, jint msg_size, jint num_of_msgs)
{
	printf("inside createMsgPooNative method\n");
	
	int total_size = num_of_msgs * msg_size;
	buf = (char*)malloc(total_size * sizeof(char));
	if(buf== NULL){
		fprintf(stderr, "Error, Could not allocate memory for Msg pool");
		return NULL;
	}

	mr = xio_reg_mr(buf, total_size);
	if(mr == NULL){
		fprintf(stderr, "Error, Could not register memory for Msg pool");
		return NULL;
	}
	printf("requested memory was successfuly allocated and regitered\n");

	jobject jbuf = env->NewDirectByteBuffer(buf, total_size );
	
	if(invoke_on_event_callback()){
		printf("Error invoking the callback to JAVA");
	}	
	
	printf("finished createMsgPooNative method\n");
	return jbuf;
}

extern "C" JNIEXPORT void JNICALL Java_com_mellanox_JXBridge_destroyMsgPoolNative()
{
	if(xio_dereg_mr(&mr) != 0){
		fprintf(stderr, "Error, Could not free the registered memory");
	}
}



extern "C" JNIEXPORT jlongArray JNICALL Java_com_mellanox_JXBridge_startClientSessionNative(JNIEnv *env, jclass cls, jstring jhostname, jint port, jlong ptrCtx) {

	struct xio_session	*session;
	struct xio_connection * con;
	struct xio_session_ops ses_ops;
	struct xio_session_attr attr;
	struct xio_context *ctx;
	char			url[256];
	jlongArray dataToJava;
	jlong temp[2];


	ctx = (struct xio_context *)ptrCtx;
	
	const char *hostname = env->GetStringUTFChars(jhostname, NULL);

	sprintf(url, "rdma://%s:%d", hostname, port);

	//defining structs to send to xio library
	ses_ops.on_session_event		=  on_session_event_callback;
	ses_ops.on_session_established		=  on_session_established_callback;
	ses_ops.on_msg				=  on_msg_callback;
	ses_ops.on_msg_error			=  NULL;

	attr.ses_ops = &ses_ops; /* callbacks structure */
	attr.user_context = NULL;	  /* no need to pass the server private data */
	attr.user_context_len = 0;

	session = xio_session_open(XIO_SESSION_REQ,
				   &attr, url, 0, ctx);
	env->ReleaseStringUTFChars(jhostname, hostname);

	if (session == NULL){
		printf("Error in creating session\n");
		return NULL;
	}

	/* connect the session  */
	con = xio_connect(session, ctx, 0, ctx);

	if (con == NULL){
		printf("Error in creating connection\n");
		return NULL;
	}

	dataToJava = env->NewLongArray(2);
	if (dataToJava == NULL) {
		printf("Error in allocating array via jni\n");
		 return NULL;
	 }
	 // fill a temp structure to use to populate the java long array

	 temp[0] = (jlong)(intptr_t) session;
	 temp[1] = (jlong)(intptr_t) con;

	 // move from the temp structure to the java structure
	 env->SetLongArrayRegion(dataToJava,0, 2, temp);

	 printf("startClientSession done with \n");


//	 printf("for debugging \n");
	 struct xio_msg *req = (struct xio_msg *) malloc(sizeof(struct xio_msg));

	 // create "hello world" message
	 memset(req, 0, sizeof(req));
	 req->out.header.iov_base = strdup("hello world header request");
	 req->out.header.iov_len = strlen("hello world header request");
	 	// send first message
	 xio_send_request(con, req);
//	 printf("done debugging \n");


	 return dataToJava;
}


extern "C" JNIEXPORT jstring JNICALL Java_com_mellanox_JXBridge_getErrorNative(JNIEnv *env, jclass cls, jint errorReason) {

	struct xio_session	*session;
	const char * error;
	jstring str;
	
	error = xio_strerror(errorReason);
	str = env->NewStringUTF(error);
//	free(error); TODO: to free it????
	return str;
 
}



extern "C" JNIEXPORT jint JNICALL Java_com_mellanox_JXBridge_CloseSessioNative(JNIEnv *env, jint session_id) {
	// i need to build a session manager here, in order to identify the session object by its id and close it.
	int retval = xio_session_close(NULL);
	if (retval != 0) {
		fprintf(stderr, "session close failed");
	}
	return retval;
}


extern "C" JNIEXPORT jint JNICALL Java_com_mellanox_JXBridge_sendMsgNative(JNIEnv *env, jint session_id, jcharArray data, jint lenght) {
	return 0;
}

/*
extern "C" JNIEXPORT jlongArray JNICALL Java_com_mellanox_JXBridge_startServerSessionNative(JNIEnv *env, jclass cls, jstring jhostname, jint port) {

	printf("inside startServerSessioNative method\n");

	struct xio_server_ops server_ops;	
	server_ops.on_session_event			    =  on_session_event_callback;
	server_ops.on_msg	  		        	=  on_msg_callback;
	server_ops.on_msg_error				    =  on_msg_error_callback;
	server_ops.on_new_session		        =  on_new_session_callback;
	server_ops.on_msg_send_complete         =  on_msg_send_complete_callback;


	struct xio_server	*server;	// server portal 
	char			url[256];
	struct xio_context	*ctx;
	void			*loop;

	// open default event loop 
	loop	= xio_ev_loop_init();

	// create thread context for the client 
	ctx	= xio_ctx_open(NULL, loop);

	const char *hostname = env->GetStringUTFChars(jhostname, NULL);
	// create url to connect to 
	sprintf(url, "rdma://%s:%d", hostname, port);
	// bind a listener server to a portal/url 
	server = xio_bind(ctx, &server_ops, url, ctx);


	env->ReleaseStringUTFChars(jhostname, hostname);
	if (server == NULL){
		printf("Error in binding server\n");
		return NULL;
	}
	
	printf("Server is now bind to address\n");
	printf("finished startServerSessioNative method\n");
	jlongArray arr =  env->NewLongArray(3);
	return arr;
}
*/


extern "C" JNIEXPORT jlong JNICALL Java_com_mellanox_JXBridge_startServerNative(JNIEnv *env, jclass cls, jstring jhostname, jint port, jlong ptrCtx) {

	printf("inside startServerNative method\n");

	struct xio_server_ops server_ops;	
	server_ops.on_session_event			    =  on_session_event_callback;
	server_ops.on_msg	  		        	=  on_msg_callback; //TODO: to separate into 2 different classes
	server_ops.on_msg_error				    =  NULL;
	server_ops.on_new_session		        =  on_new_session_callback;
	server_ops.on_msg_send_complete         =  NULL;


	struct xio_server	*server;	/* server portal */
	char			url[256];
	struct xio_context	*ctx;
	jlong			ptr;
	jlongArray dataToJava;
	jlong temp[2];

	ctx = (struct xio_context *)ptrCtx;



	const char *hostname = env->GetStringUTFChars(jhostname, NULL);


	/* create url to connect to */
	sprintf(url, "rdma://%s:%d", hostname, port);

	env->ReleaseStringUTFChars(jhostname, hostname);
	/* bind a listener server to a portal/url */

	server = xio_bind(ctx, &server_ops, url, ctx);
	if (server == NULL){
		printf("Error in binding server\n");
		return 0;
	}
	
	ptr = (jlong)(intptr_t) server;
	printf("Server is now bind to address\n");
	printf("finished startServerNative method\n");

	return ptr;
}


extern "C" JNIEXPORT jboolean JNICALL Java_com_mellanox_JXBridge_forwardSessionNative(JNIEnv *env, jclass cls, jstring jhostname, jint port, jlong ptrSession) {

	struct xio_session	*session;	
	char			portal[256];
	const char* constPortal[1];
	int retVal;


	const char *hostname = env->GetStringUTFChars(jhostname, NULL);

	sprintf(portal, "rdma://%s:%d", hostname, port);

	constPortal[0] = (const char*)portal;

	session = (struct xio_session *)ptrSession;
	
	retVal = xio_accept (session, constPortal, 1, NULL, 0);

//	retVal = xio_accept(session, NULL, 0, NULL, 0);

	env->ReleaseStringUTFChars(jhostname, hostname);

    if (retVal){
		printf("Error in accepting session. error %d\n", retVal);
		return false;
	}
	return true;
	
}



JNIEnv *JX_attachNativeThread()
{
    JNIEnv *env;
	if (! cached_jvm) {
		printf("cached_jvm is NULL");
	}
    jint ret = cached_jvm->AttachCurrentThread((void **)&env, NULL);

	if (ret < 0) {
		printf("cached_jvm->AttachCurrentThread failed ret=%d", ret);
	}
	printf("completed successfully env=%p", env);
    return env; // note: this handler is valid for all functions in this thread
}







