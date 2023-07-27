
### 先配置Github的用户名和名字
- git config --global user.name "AzollaGit"
- git config --global user.email 2220814784@qq.com
- ssh-keygen -t rsa -C "2220814784@qq.com"
- cat ~/.ssh/id_rsa.pub	(id_rsa.pub文件内的内容，粘帖到github帐号管理的添加SSH key界面中)

### create a new repository on the command line
- echo "# YiNodeMesh" >> README.md
- git init
- git add README.md
- git commit -m "first commit"
- git branch -M master
- git remote remove origin   (删除远程链接)
- git remote add origin git@github.com:AzollaGit/YiRootTemp.git
- git push -u origin master

### push an existing repository from the command line
- git remote add origin git@github.com:AzollaGit/YiRootTemp.git
- git branch -M master
- git push -u origin master