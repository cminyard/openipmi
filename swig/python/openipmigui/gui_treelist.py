# gui_treelist.py
#
# A tree/list widget
#
# Author: MontaVista Software, Inc.
#         Corey Minyard <minyard@mvista.com>
#         source@mvista.com
#
# Copyright 2006 MontaVista Software Inc.
#
#  This program is free software; you can redistribute it and/or
#  modify it under the terms of the GNU Lesser General Public License
#  as published by the Free Software Foundation; either version 2 of
#  the License, or (at your option) any later version.
#
#
#  THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESS OR IMPLIED
#  WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
#  MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
#  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
#  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
#  BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
#  OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
#  ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR
#  TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE
#  USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
#
#  You should have received a copy of the GNU Lesser General Public
#  License along with this program; if not, write to the Free
#  Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
#

import wx
import wx.gizmos as gizmos
import gui_errstr

class TreeList(wx.Dialog):
    def __init__(self, name, root, columns):
        wx.Dialog.__init__(self, None, -1, name,
                           size=wx.Size(400, 600),
                           style=wx.RESIZE_BORDER)

        sizer = wx.BoxSizer(wx.VERTICAL)

        tree = gizmos.TreeListCtrl(self)
        i = 0
        for c in columns:
            tree.AddColumn(c[0])
            tree.SetColumnWidth(i, c[1])
            i += 1
            pass
        tree.SetMainColumn(0)
        
        sizer.Add(tree, 1, wx.GROW, 0)

        self.errstr = gui_errstr.ErrStr(self)
        sizer.Add(self.errstr, 0, wx.ALIGN_CENTRE | wx.ALL | wx.GROW, 5)
        
        box = wx.BoxSizer(wx.HORIZONTAL)
        if hasattr(self, "ok"):
            ok = wx.Button(self, -1, "Ok")
            wx.EVT_BUTTON(self, ok.GetId(), self.ok)
            box.Add(ok, 0, wx.ALIGN_CENTRE | wx.ALL, 2)
            pass
        if hasattr(self, "save"):
            ok = wx.Button(self, -1, "Save")
            wx.EVT_BUTTON(self, ok.GetId(), self.save)
            box.Add(ok, 0, wx.ALIGN_CENTRE | wx.ALL, 2)
            pass
        if hasattr(self, "cancel"):
            ok = wx.Button(self, -1, "Cancel")
            wx.EVT_BUTTON(self, ok.GetId(), self.cancel)
            box.Add(ok, 0, wx.ALIGN_CENTRE | wx.ALL, 2)
            pass
        sizer.Add(box, 0, wx.ALIGN_CENTRE | wx.ALL, 2)

        treeroot = tree.AddRoot(root)

        wx.EVT_TREE_ITEM_RIGHT_CLICK(tree, -1, self.TreeMenu)

        self.SetSizer(sizer)
        wx.EVT_CLOSE(self, self.OnClose)
        self.CenterOnScreen();

        self.tree = tree
        self.treeroot = treeroot

        return

    def OnClose(self, event):
        self.Destroy()
        return

    def TreeMenu(self, event):
        eitem = event.GetItem()
        data = self.tree.GetPyData(eitem)
        if (data and hasattr(data, "HandleMenu")):
            rect = self.tree.GetBoundingRect(eitem)
            if (rect == None):
                point = None
            else:
                # FIXME - why do I have to add 25?
                point = wx.Point(rect.GetLeft(), rect.GetBottom()+25)
                pass
            data.HandleMenu(event, eitem, point)
            pass
        return

    def AfterDone(self):
        self.tree.Expand(self.treeroot)
        self.Show(True)
        return

    def Append(self, node, name, values, data=None):
        sub = self.tree.AppendItem(node, name)
        i = 1
        for v in values:
            if (v != None):
                self.tree.SetItemText(sub, v, i)
            i += 1
            pass
        if (data != None):
            self.tree.SetPyData(sub, data)
            pass
        return sub

    def SetColumn(self, node, value, colnum):
        self.tree.SetItemText(node, value, colnum)
        return

    def GetColumn(self, node, colnum):
        return self.tree.GetItemText(node, colnum)

    def SetError(str):
        self.errstr.SetError(str)
        return
    pass
