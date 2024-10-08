#
# Copyright 2019 Pixar
#
# Licensed under the terms set forth in the LICENSE.txt file available at
# https://openusd.org/license.
#
from .qt import QtCore, QtGui, QtWidgets

from pxr import Tf


# Returns a list of debug flags whose names are prefixed by debugFlagPrefix
# followed by an '_'.
#
def _GetDebugFlagsWithPrefix(debugFlagPrefix):
    debugFlagPrefix += "_"
    return [flag for flag in Tf.Debug.GetDebugSymbolNames() 
            if flag.startswith(debugFlagPrefix)]
    
# Main DebugFlags editor widget
#
class DebugFlagsWidget(QtWidgets.QWidget):

    def __init__(self, parent=None):

        super(DebugFlagsWidget, self).__init__(parent)

        self.setObjectName("Debug Flags")
        self.setMinimumSize(640, 300)
        self._listView = QtWidgets.QListView()
        self._tableWidget = QtWidgets.QTableWidget(0, 2)


        # Configure and populate the left list view
        self._listView.setEditTriggers(
            QtWidgets.QAbstractItemView.NoEditTriggers)

        self._populateDebugFlagsListView(self._listView)

        self._listView.selectionModel().selectionChanged.connect(
            self._onFlagSelectionChanged)


        # Configure the table widget
        self._tableWidget.horizontalHeader().setStretchLastSection(True)
        self._tableWidget.horizontalHeader().setDefaultSectionSize(200)
        self._tableWidget.setHorizontalHeaderLabels(
                    ['Debug Symbol', 'Description'])
        self._tableWidget.verticalHeader().hide()

        self._tableWidget.itemClicked.connect(self._onDebugFlagChecked)
        
        # Set the layout
        lay = QtWidgets.QHBoxLayout()
        lay.addWidget(self._listView, 0)
        lay.addWidget(self._tableWidget, 1)
        self.setLayout(lay)

    def _populateDebugFlagsListView(self, listView):
        allDebugFlags = Tf.Debug.GetDebugSymbolNames()
        allDebugPrefixes = [ x[:x.find('_')] if x.find('_') > 0 else x
                for x in allDebugFlags]
        self._allDebugFlagPrefixes = list(sorted(set(allDebugPrefixes)))
        listModel = QtCore.QStringListModel(self._allDebugFlagPrefixes)
        listView.setModel(listModel)

    def _populateDebugFlagsTableView(self, debugFlagPrefix):
        debugFlags = _GetDebugFlagsWithPrefix(debugFlagPrefix)

        self._tableWidget.setRowCount(len(debugFlags))
        row = 0
        for f in debugFlags:
            item = QtWidgets.QTableWidgetItem()
            item.setFlags(QtCore.Qt.ItemIsUserCheckable | 
                          QtCore.Qt.ItemIsEnabled)
            if Tf.Debug.IsDebugSymbolNameEnabled(f):
                item.setCheckState(QtCore.Qt.Checked)
            else:
                item.setCheckState(QtCore.Qt.Unchecked)
            item.setText(f)
            self._tableWidget.setItem(row, 0, item)

            item = QtWidgets.QTableWidgetItem()
            item.setFlags(item.flags() ^ QtCore.Qt.ItemIsEditable)
            item.setText(Tf.Debug.GetDebugSymbolDescription(f))
            self._tableWidget.setItem(row, 1, item)

            row += 1

    def _onFlagSelectionChanged(self, selected, deselected):
        if len(selected.indexes()) > 0:
            self._populateDebugFlagsTableView(self._allDebugFlagPrefixes[
                selected.indexes()[0].row()])

    def _onDebugFlagChecked(self, item):
        value = (item.checkState() == QtCore.Qt.Checked)
        Tf.Debug.SetDebugSymbolsByName(item.text(), value)
