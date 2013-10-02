/*
** Copyright (C) 2013 Mellanox Technologies
**
** Licensed under the Apache License, Version 2.0 (the "License");
** you may not use this file except in compliance with the License.
** You may obtain a copy of the License at:
**
** http://www.apache.org/licenses/LICENSE-2.0
**
** Unless required by applicable law or agreed to in writing, software
** distributed under the License is distributed on an "AS IS" BASIS,
** WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
** either express or implied. See the License for the specific language
** governing permissions and  limitations under the License.
**
*/
package com.mellanox.jxio.impl;

import java.util.logging.Level;
import com.mellanox.jxio.Log;

public class Bridge {

    static {  
		String s1=Bridge.class.getProtectionDomain().getCodeSource().getLocation().getPath(); 
		String s2=s1.substring(0, s1.lastIndexOf("/")+1);
		Runtime.getRuntime().load(s2 + "libjx.so");
    }
	private static Log logger = Log.getLog(Bridge.class.getCanonicalName());

	// Native methods and their wrappers start here
    
	
	private static native boolean createCtxNative(int eventQueueSize, Object dataFromC);
	public static boolean createCtx(int eventQueueSize, Object dataFromC) {
		logger.log(Level.FINE, "invoking createCtxNative");
		boolean ret = createCtxNative(eventQueueSize, dataFromC);
		logger.log(Level.FINE, "finished createCtxNative");
		return ret;
		
	}
	private static native void closeCtxNative(long ptr);
	public static void closeCtx(long ptr) {
		logger.log(Level.FINE, "invoking closeCtxNative");
		closeCtxNative(ptr);
		logger.log(Level.FINE, "finished closeCtxNative" );
			
	}
	private static native int runEventLoopNative(long ptr, long timeOutMicroSec);
	public static int runEventLoop(long ptr, long timeOutMicroSec) {
	    logger.log(Level.FINE, "invoking Bridge.runEventLoopNative");
	    int ret = runEventLoopNative(ptr, timeOutMicroSec);
	    logger.log(Level.FINE, "finished Bridge.runEventLoopNative=" + ret);
	    return ret;
	}	
	private static native void stopEventLoopNative(long ptr);
	public static void stopEventLoop(long ptr) {
	    logger.log(Level.FINE, "invoking stopEventLoopNative");
	    stopEventLoopNative(ptr);
	    logger.log(Level.FINE, "finished stopEventLoopNative");
	}
	private static native int addEventLoopFdNative(long ptr, long fd, int events, long priv_data);
	public static int addEventLoopFd(long ptr, long fd, int events, long priv_data) {
	    logger.log(Level.FINE, "invoking addEventLoopFdNative");
	    int ret = addEventLoopFdNative(ptr, fd, events, priv_data);
	    logger.log(Level.FINE, "finished addEventLoopFdNative");
	    return ret;
	}
	private static native int delEventLoopFdNative(long ptr, long fd);
	public static int delEventLoopFd(long ptr, long fd) {
	    logger.log(Level.FINE, "invoking delEventLoopFdNative");
	    int ret = delEventLoopFdNative(ptr, fd);
	    logger.log(Level.FINE, "finished delEventLoopFdNative");
	    return ret;
	}

	private static native long startSessionClientNative(String url, long ptrCtx);
	public static long startSessionClient(String url, long ptrCtx) {
		logger.log(Level.FINE, "invoking startSessionNative");
		long p = startSessionClientNative(url, ptrCtx);		
		logger.log(Level.FINE, "finished startSessionNative ");
		return p;
	}	
	private static native void closeSessionClientNative(long sesPtr);
	public static void closeSessionClient(long sesPtr) {
		logger.log(Level.FINE, "invoking Bridge.closeSessionClient");
		closeSessionClientNative(sesPtr);
		logger.log(Level.FINE, "finished Bridge.closeSessionClient");
	}

	private static native long [] startServerNative(String url, long ptrCtx);
	public static long [] startServer(String url, long ptrCtx) {
		logger.log(Level.FINE, "invoking startServerNative");
		long ptr [] = startServerNative(url, ptrCtx);
		logger.log(Level.FINE, "finished startServerNative");
		return ptr;
	}
	private static native boolean stopServerNative(long ptr);
	public static boolean stopServer(long ptr) {
		logger.log(Level.FINE, "invoking stopServerNative");
		boolean ret = stopServerNative(ptr);
		logger.log(Level.FINE, "finished stopServerNative ret=" + ret);
		return ret;
	}

	private static native long forwardSessionNative(String url, long ptrSes, long ptrServer);
	public static long forwardSession(String url, long ptrSes, long ptrServer) {
		logger.log(Level.FINE, "invoking forwardSessionNative");
		long ptr = forwardSessionNative(url, ptrSes, ptrServer);
		logger.log(Level.FINE, "finished forwardSessionNative");
		return ptr;
	}

	private static native long createMsgPoolNative(int count, int inSize, int outSize);
	public static long  createMsgPool(int count, int inSize, int outSize) {
		logger.log(Level.FINE, "invoking createMsgPoolNative");

		long ptr = createMsgPoolNative(count, inSize, outSize);
		logger.log(Level.FINE, "finished createMsgPoolNative");
		return ptr;

	}

	private static native boolean sendMsgNative(long ptrSession, int sessionType, long ptrMsg);
	public static boolean  sendMsg(long ptrSession, int sessionType, long ptrMsg) {
		logger.log(Level.FINE, "invoking sendMsgNative");
		boolean ret = sendMsgNative(ptrSession, sessionType, ptrMsg);
		logger.log(Level.FINE, "finished sendMsgNative ret= "+ ret);
		return ret;
	}

	private static native String getErrorNative(int errorReason);
	public static String getError(int errorReason) {
//		logger.log(Level.FINE, "invoking getErrorNative");
		String s = getErrorNative(errorReason);
//		logger.log(Level.FINE, "finished getErrorNative. error was "+s);
		return s;
	}
}