/****************************************************************************
**
** Copyright (C) 2017 The Qt Company Ltd.
** Contact: https://www.qt.io/licensing/
**
** This file is part of the FOO module of the Qt Toolkit.
**
** $QT_BEGIN_LICENSE:GPL-EXCEPT$
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and The Qt Company. For licensing terms
** and conditions see https://www.qt.io/terms-conditions. For further
** information use the contact form at https://www.qt.io/contact-us.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU
** General Public License version 3 as published by the Free Software
** Foundation with exceptions as appearing in the file LICENSE.GPL3-EXCEPT
** included in the packaging of this file. Please review the following
** information to ensure the GNU General Public License requirements will
** be met: https://www.gnu.org/licenses/gpl-3.0.html.
**
** $QT_END_LICENSE$
**
****************************************************************************/


function Component()
{
    function stopAndDeleteService() {
        var result = installer.execute("sc",["stop", "px_service"]);
        console.log("===> [sc stop px_service] result: ", result);

        result = installer.execute("taskkill", ["/F", "/IM", "px_service.exe"]);
        console.log("===> [taskkill px_service.exe] result: ", result);

        result = installer.execute("taskkill", ["/F", "/IM", "px_service_manager.exe"]);
        console.log("===> [taskkill px_service_manager.exe] result: ", result);

        result = installer.execute("sc",["delete", "px_service"]);
        console.log("===> [sc delete px_service] result: ", result);
    }

    //installer.setDefaultPageVisible(QInstaller.Introduction, false);

    if (installer.isInstaller()) {


        var result = installer.execute("sc",["query", "px_service"]);
        console.log("===>[sc query px_service] result: ");

        var running = false;
        result.forEach(function(element) {
            console.log("line: ", element);
            if (typeof element !== 'string') {
                return;
            }

            if (element.includes("RUNNING")) {
                running = true;
            }
        });
        console.log("Is px_service running? ", running);

        var targetDir = installer.value("TargetDir");
        console.log("Installation dir: ", targetDir);

        if (running) {
            var answer = QMessageBox.question("diglog_remove_exists", "Uninstall Service", "Do you want to uninstall Service?", QMessageBox.Yes | QMessageBox.No);
            if (answer === QMessageBox.Yes) {
                console.log("User chose to continue installation.");
                stopAndDeleteService();
            } else {
                console.log("User chose to cancel installation.");
                answer = QMessageBox.question("diglog_quiting", "Exiting", "Installation will exit.", QMessageBox.Yes);
                gui.clickButton(buttons.CancelButton);
            }
        }

        console.log("===> folder exists, will delete it !", targetDir);
        installer.performOperation("Delete",targetDir + "/maintenancetool.exe");
        installer.performOperation("Delete",targetDir + "/maintenancetool.dat");
        installer.performOperation("Delete",targetDir + "/maintenancetool.ini");
        installer.performOperation("Delete", targetDir);

        installer.installationFinished.connect(function() {
            console.log("===> Install finished.");
            installer.performOperation("Execute", targetDir + "/px_panel.exe");
            //installer.executeDetached(targetDir + "/px_panel.exe");
        });

    }
    else if (installer.isUninstaller()) {

    }

    installer.uninstallationStarted.connect(function() {
        console.log("===> UnInstallation started.");
        console.log("User chose to continue installation.");
        stopAndDeleteService();
    });
}

Component.prototype.beginInstallation = function()
{
    // call default implementation
    component.beginInstallation();

    console.log("===> Installation Started ===");

}


Component.prototype.createOperations = function()
{

     console.log("===> Create Operations...");

    // call default implementation to actually install README.txt!
    component.createOperations();

    component.addOperation("CreateShortcut", "@TargetDir@/px_panel.exe", "@StartMenuDir@/px_panel.lnk",
        "workingDirectory=@TargetDir@", "iconPath=@TargetDir@/px_icon.ico",
        "description=Open px_panel");

    component.addOperation("CreateShortcut", "@TargetDir@/px_panel.exe", "@DesktopDir@/px_panel.lnk", "iconPath=@TargetDir@/px_icon.ico",
    "workingDirectory=@TargetDir@");

    // component.addOperation("CreateShortcut", "@TargetDir@/px_client.exe", "@StartMenuDir@/px_client.lnk",
    //     "workingDirectory=@TargetDir@", "iconPath=@TargetDir@/px_client_icon.ico",
    //     "description=Open px_client");

    // component.addOperation("CreateShortcut", "@TargetDir@/px_client.exe", "@DesktopDir@/px_client.lnk", "iconPath=@TargetDir@/px_client_icon.ico",
    // "workingDirectory=@TargetDir@");
}
