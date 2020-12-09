/*
 * deprecator.h
 *
 * MontaVista IPMI deprecation defines
 *
 * Author: MontaVista Software, Inc.
 *         Corey Minyard <minyard@mvista.com>
 *         source@mvista.com
 *
 * Copyright 2020 MontaVista Software Inc.
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public License
 *  as published by the Free Software Foundation; either version 2 of
 *  the License, or (at your option) any later version.
 *
 *
 *  THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESS OR IMPLIED
 *  WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 *  MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 *  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 *  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 *  BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
 *  OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 *  ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR
 *  TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE
 *  USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this program; if not, write to the Free
 *  Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifndef OpenIPMI_DLLVISIBILITY
#define OpenIPMI_DLLVISIBILITY

/*
 * GCC seems to support __declspec(dllexport), but I am worred about older
 * compiler support on CYGWIN, so I'm leaving __attribute__ ((dllexport)) in.
 */
#if defined _WIN32 || defined __CYGWIN__
  #ifdef BUILDING_IPMI_DLL
    #ifdef __GNUC__
      #define IPMI_DLL_PUBLIC __attribute__ ((dllexport))
    #else
      #define IPMI_DLL_PUBLIC __declspec(dllexport)
    #endif
  #else
    #ifdef __GNUC__
      #define IPMI_DLL_PUBLIC __attribute__ ((dllimport))
    #else
      #define IPMI_DLL_PUBLIC __declspec(dllimport)
    #endif
  #endif
  #if defined(BUILDING_IPMI_UTILS_DLL)
    #ifdef __GNUC__
      #define IPMI_UTILS_DLL_PUBLIC __attribute__ ((dllexport))
    #else
      #define IPMI_UTILS_DLL_PUBLIC __declspec(dllexport)
    #endif
  #else
    #ifdef __GNUC__
      #define IPMI_UTILS_DLL_PUBLIC __attribute__ ((dllimport))
    #else
      #define IPMI_UTILS_DLL_PUBLIC __declspec(dllimport)
    #endif
  #endif
  #define IPMI_DLL_LOCAL
  #define IPMI_UTILS_DLL_LOCAL
#else
  #if __GNUC__ >= 4
    #define IPMI_DLL_PUBLIC __attribute__ ((visibility ("default")))
    #define IPMI_DLL_LOCAL  __attribute__ ((visibility ("hidden")))
    #define IPMI_UTILS_DLL_PUBLIC __attribute__ ((visibility ("default")))
    #define IPMI_UTILS_DLL_LOCAL  __attribute__ ((visibility ("hidden")))
  #else
    #define IPMI_DLL_PUBLIC
    #define IPMI_DLL_LOCAL
    #define IPMI_UTILS_DLL_PUBLIC
    #define IPMI_UTILS_DLL_LOCAL
  #endif
#endif

#endif /* OpenIPMI_DLLVISIBILITY */
