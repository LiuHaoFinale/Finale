/*
 * @Author: LiuHao
 * @Date: 2024-03-31 18:18:51
 * @Description: 解决vscode的cmake自动配置问题
 */
import * as vscode from 'vscode';

export function activate(context: vscode.ExtensionContext) {
    console.log('Congratulations, your extension "myExtension" is now active!');

    let disposable = vscode.commands.registerCommand('extension.modifySettings', () => {
        // 获取配置
        let config = vscode.workspace.getConfiguration();
        
        // 修改配置
        config.update('editor.fontSize', 14, vscode.ConfigurationTarget.Global)
            .then(() => {
                vscode.window.showInformationMessage('Font size updated successfully!');
            }, (error) => {
                vscode.window.showErrorMessage(`Failed to update font size: ${error}`);
            });
    });

    context.subscriptions.push(disposable);
}

export function deactivate() {}
