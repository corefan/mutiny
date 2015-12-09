#include "ProjectBuilder.h"
#include "Environment.h"
#include "cwrapper.h"
#include "FileInfo.h"
#include "Util.h"
#include "Compiler.h"
#include "features.h"

#include <iostream>

arc<ProjectBuilder> ProjectBuilder::create(arc<Environment> environment)
{
  arc<ProjectBuilder> rtn = arc<ProjectBuilder>::alloc();

  rtn->environment = environment;
  rtn->srcDir = "assets";

  std::cout << "Project Directory: " << rtn->srcDir << std::endl;

  rtn->outputDirectory = Util::fixPath("build/") +
    PLATFORM_NAME;

  if(environment->getOutputDirectory() != "")
  {
    rtn->outputDirectory = environment->getOutputDirectory();
  }

  rtn->obtainOutputFilename();

  arc<Compiler> compiler = Compiler::create(environment);

  if(compiler->getExecutableSuffix() != "")
  {
    rtn->outputFilename += "." + compiler->getExecutableSuffix();
  }

  rtn->scanForIncludeDirectories(rtn->srcDir);
  rtn->scanSource(rtn->srcDir);
  rtn->prepareDirectories();

  return rtn;
}

void ProjectBuilder::removeOrphanedObjects()
{
  std::string objDir = Util::fixPath("temp/"+std::string(PLATFORM_NAME)+"/obj/" + outputFilename);

  arc<Dir> dir = Dir::opendir(objDir);
  arc<Dirent> dirent = dir->readdir();
  std::vector<std::string> toDelete;

  while(dirent.get() != NULL)
  {
    if(dirent->d_name().at(0) == '.')
    {
      dirent = dir->readdir();
      continue;
    }

    bool found = false;

    for(int i = 0; i < sourceUnits.size(); i++)
    {
      if(sourceUnits.at(i)->getBaseName() ==
         FileInfo::getBaseName(objDir + DIR_CHAR + dirent->d_name()))
      {
        found = true;
        break;
      }
    }

    if(found == false)
    {
      toDelete.push_back(objDir + DIR_CHAR + dirent->d_name());
    }

    dirent = dir->readdir();
  }

  for(int i = 0; i < toDelete.size(); i++)
  {
    std::cout << "Deleting: " << toDelete.at(i) << std::endl;
    Dir::remove(toDelete.at(i));
  }
}

void ProjectBuilder::obtainOutputFilename()
{
  outputFilename = FileInfo::getFileName(Dir::getcwd());
}

void ProjectBuilder::syncAssetDirectory()
{
  std::vector<arc<FileInfo> > assetDirectories;
  std::vector<arc<FileInfo> > buildAssetDirectories;
  std::vector<std::string> directoryDeleteList;

  std::string assetDirectory = Util::fixPath("assets");

  std::string buildAssetDirectory = Util::fixPath("build/" +
    std::string(PLATFORM_NAME)+"/share/" + outputFilename);

  FileInfo::scanDirectory(assetDirectory, false, assetDirectories);
  FileInfo::scanDirectory(buildAssetDirectory, false, buildAssetDirectories);
  FileInfo::scanDirectory(assetDirectory, true, assetDirectories);
  FileInfo::scanDirectory(buildAssetDirectory, true, buildAssetDirectories);

  assetDirectory += DIR_CHAR;
  buildAssetDirectory += DIR_CHAR;

  for(int i = assetDirectories.size() - 1; i >= 0; i--)
  {
    bool found = false;

    for(int j = 0; j < buildAssetDirectories.size(); j++)
    {
      if(assetDirectories.at(i)->getAbsolutePath().
           substr(assetDirectory.length()) ==
         buildAssetDirectories.at(j)->getAbsolutePath().
           substr(buildAssetDirectory.length()))
      {
        if(assetDirectories.at(i)->getModified() <=
           buildAssetDirectories.at(j)->getModified())
        {
          found = true;
        }

        break;
      }
    }

    if(found == false)
    {
      if(Dir::isdir(assetDirectories.at(i)->getAbsolutePath()) == true)
      {
        Dir::mkdir(buildAssetDirectory + assetDirectories.at(i)->
          getAbsolutePath().substr(assetDirectory.length()));
      }
      else
      {
        Util::copyFile(assetDirectories.at(i)->getAbsolutePath(),
          buildAssetDirectory + assetDirectories.at(i)->
          getAbsolutePath().substr(assetDirectory.length()));
      }
    }
  }
}

void ProjectBuilder::removeOrphanedAssets()
{
  std::vector<arc<FileInfo> > assetDirectories;
  std::vector<arc<FileInfo> > buildAssetDirectories;
  std::vector<std::string> directoryDeleteList;

  std::string assetDirectory = Util::fixPath("assets");
  std::string buildAssetDirectory = Util::fixPath("build/"+std::string(PLATFORM_NAME)+"/share/" + outputFilename);

  FileInfo::scanDirectory(assetDirectory, false, assetDirectories);
  FileInfo::scanDirectory(buildAssetDirectory, false, buildAssetDirectories);
  FileInfo::scanDirectory(assetDirectory, true, assetDirectories);
  FileInfo::scanDirectory(buildAssetDirectory, true, buildAssetDirectories);

  assetDirectory += DIR_CHAR;
  buildAssetDirectory += DIR_CHAR;

  for(int i = 0; i < buildAssetDirectories.size(); i++)
  {
    bool found = false;

    for(int j = 0; j < assetDirectories.size(); j++)
    {
      if(buildAssetDirectories.at(i)->getAbsolutePath().substr(buildAssetDirectory.length()) ==
         assetDirectories.at(j)->getAbsolutePath().substr(assetDirectory.length()))
      {
        found = true;
        break;
      }
    }

    if(found == false)
    {
      directoryDeleteList.push_back(buildAssetDirectories.at(i)->getAbsolutePath());
    }
  }

  for(int i = 0; i < directoryDeleteList.size(); i++)
  {
    //std::cout << "Dir::remove(" << directoryDeleteList.at(i) << ");" << std::endl;
    Dir::remove(directoryDeleteList.at(i));
  }
}

void ProjectBuilder::generateOutOfDateOutput()
{
  arc<Compiler> compiler = Compiler::create(environment);

  if(environment->isMutinyAvailable() == true)
  {
    compiler->addObjectDirectory(Util::fixPath("temp/"+std::string(PLATFORM_NAME)+"/obj/mutiny"));
  }

  if(compiler->getName() == "cl")
  {
    compiler->addLibDirectory(environment->getPrefix() + Util::fixPath("/import/lib"));
  }

  compiler->addObjectDirectory(Util::fixPath("temp/"+std::string(PLATFORM_NAME)+"/obj/" + outputFilename));

  for(int i = 0; i < libs.size(); i++)
  {
    compiler->addLib(libs.at(i)->getAbsolutePath());
  }

  bool needsRelink = false;

  try
  {
    arc<FileInfo> binInfo = FileInfo::create(outputDirectory + Util::fixPath("/bin/") + outputFilename);

    for(int i = 0; i < sourceUnits.size(); i++)
    {
      if(sourceUnits.at(i)->getModified() > binInfo->getModified())
      {
        needsRelink = true;
      }
    }

    for(int i = 0; i < libs.size(); i++)
    {
      if(libs.at(0)->getModified() > binInfo->getModified())
      {
        needsRelink = true;
      }
    }
  }
  catch(std::exception& e)
  {
    needsRelink = true;
  }

  removeOrphanedAssets();
  syncAssetDirectory();

  if(needsRelink == false) return;

  //std::cout << "Linking..." << std::endl;
  compiler->link(outputDirectory + Util::fixPath("/bin/") + outputFilename);
}

void ProjectBuilder::buildOutOfDateObjects()
{
  arc<Compiler> compiler = Compiler::create(environment);
  compiler->addIncludeDirectory(environment->getPrefix() + Util::fixPath("/src"));

  if(compiler->getName() == "cl")
  {
    compiler->addIncludeDirectory(environment->getPrefix() + Util::fixPath("/import/include"));
  }

  for(int i = 0; i < includeDirectories.size(); i++)
  {
    compiler->addIncludeDirectory(includeDirectories.at(i));
  }

  for(int i = 0; i < sourceUnits.size(); i++)
  {
    std::string objPath = Util::fixPath("temp/"+std::string(PLATFORM_NAME)+"/obj/"+outputFilename+"/" +
      sourceUnits.at(i)->getBaseName() +
      "." + compiler->getObjectSuffix());

    try
    {
      arc<FileInfo> objectInfo = FileInfo::create(objPath);

      if(objectInfo->getModified() > sourceUnits.at(i)->getModified())
      {
        //std::cout << "Up to date: " << sourceUnits.at(i)->getFileName() << std::endl;
        continue;
      }
    }
    catch(std::exception& e) { }

    //std::cout << "Compiling: " << sourceUnits.at(i)->getFileName() << std::endl;
    //std::string result = Util::execute("g++ -std=c++11 -c " +
    //  sourceUnits.at(i)->getAbsolutePath() +
    //  " -o " +
    //  objPath);

    compiler->compile(sourceUnits.at(i)->getAbsolutePath(), objPath);

    //std::cout << result << std::endl;
  }
}

void ProjectBuilder::prepareDirectories()
{
  try { Dir::mkdir_r(Util::fixPath("temp/"+std::string(PLATFORM_NAME)+"/obj/" + outputFilename)); } catch(std::exception& e) { }

  try { Dir::mkdir_r(Util::fixPath(outputDirectory + "/share/" + outputFilename)); } catch(std::exception& e) { }
  try { Dir::mkdir_r(Util::fixPath(outputDirectory + "/bin")); } catch(std::exception& e) { }
}

void ProjectBuilder::scanSource(std::string rootDir)
{
  arc<Dir> dir = Dir::opendir(rootDir);
  arc<Dirent> dirent = dir->readdir();

  while(dirent.get() != NULL)
  {
    if(dirent->d_name().at(0) != '.')
    {
      try
      {
        scanSource(rootDir + DIR_CHAR + dirent->d_name());
      }
      catch(std::exception& e)
      {
        if(FileInfo::getSuffix(dirent->d_name()) == "cpp" ||
           FileInfo::getSuffix(dirent->d_name()) == "c")
        {
          //std::cout << "Adding: " << rootDir << DIR_CHAR << dirent->d_name() << std::endl;
          sourceUnits.push_back(SourceFileInfo::create(rootDir + DIR_CHAR + dirent->d_name(), includeDirectories));
        }
        else if(FileInfo::getSuffix(dirent->d_name()) == "so" ||
                FileInfo::getSuffix(dirent->d_name()) == "lib")
        {
          libs.push_back(FileInfo::create(rootDir + DIR_CHAR + dirent->d_name()));
        }
      }
    }

    dirent = dir->readdir();
  }
}

void ProjectBuilder::scanForIncludeDirectories(std::string rootDir)
{
  arc<Dir> dir = Dir::opendir(rootDir);
  arc<Dirent> dirent = dir->readdir();

  while(dirent.get() != NULL)
  {
    if(dirent->d_name().at(0) == '.')
    {
      dirent = dir->readdir();
      continue;
    }

    try
    {
      scanForIncludeDirectories(rootDir + DIR_CHAR + dirent->d_name());

      if(dirent->d_name() == "include")
      {
        includeDirectories.push_back(rootDir + DIR_CHAR + dirent->d_name());
      }
    }
    catch(std::exception& e){}

    dirent = dir->readdir();
  }
}
