// SPDX-License-Identifier: GPL-2.0-only
package io.github.lucky_ji.wenku8_reader;

import android.content.Context;
import android.content.Intent;
import android.content.UriPermission;
import android.database.Cursor;
import android.net.Uri;
import android.provider.DocumentsContract;
import android.system.Os;
import android.system.OsConstants;
import java.io.File;
import java.io.FileInputStream;
import java.io.IOException;
import java.io.OutputStream;
import java.util.HashSet;
import java.util.Set;
import org.json.JSONObject;

/** Storage Access Framework export; no broad storage permission is requested. */
public final class ExportDocuments
{
    private ExportDocuments()
    {
    }
    private static native boolean shouldContinue(long nToken);
    private static final class Cancelled extends IOException
    {
    }
    private static void checkContinue(long nToken) throws Cancelled
    {
        if (!shouldContinue(nToken))
        {
            throw new Cancelled();
        }
    }
    private static JSONObject result(String strLocation, String strLabel, String strError)
    {
        JSONObject oResult = new JSONObject();
        try
        {
            oResult.put("location", strLocation);
            oResult.put("label", strLabel);
            oResult.put("error", strError);
        }
        catch (Exception oError)
        {
        }
        return oResult;
    }
    public static String privateDirectory(Context oContext)
    {
        return new File(oContext.getFilesDir(), "books").getAbsolutePath();
    }
    private static Uri rootDocument(String strTree)
    {
        Uri oTree = Uri.parse(strTree);
        // getTreeDocumentId and document-tree access are available from API 21.
        // isTreeUri itself requires API 24, despite tree URIs existing since API 21.
        if (!"content".equals(oTree.getScheme()) || oTree.getAuthority() == null ||
                oTree.getPathSegments().size() != 2 ||
                !"tree".equals(oTree.getPathSegments().get(0)) ||
                oTree.getPathSegments().get(1).isEmpty())
        {
            throw new IllegalArgumentException();
        }
        return DocumentsContract.buildDocumentUriUsingTree(oTree,
                    DocumentsContract.getTreeDocumentId(oTree));
    }
    public static String directoryLabel(Context oContext, String strTree)
    {
        try (Cursor oCursor = oContext.getContentResolver().query(rootDocument(strTree),
                new String[]{DocumentsContract.Document.COLUMN_DISPLAY_NAME}, null, null, null))
        {
            if (oCursor != null && oCursor.moveToFirst())
            {
                return oCursor.getString(0);
            }
        }
        catch (Exception oError)
        {
        }
        return "已选择的目录";
    }
    public static boolean directoryAvailable(Context oContext, String strTree)
    {
        try
        {
            Uri oTree = Uri.parse(strTree);
            boolean bGranted = false;
            for (UriPermission oPermission : oContext.getContentResolver().getPersistedUriPermissions())
            {
                if (oPermission.getUri().equals(oTree) && oPermission.isReadPermission() &&
                        oPermission.isWritePermission())
                {
                    bGranted = true;
                    break;
                }
            }
            if (!bGranted)
            {
                return false;
            }
            try (Cursor oCursor = oContext.getContentResolver().query(rootDocument(strTree),
                    new String[]{DocumentsContract.Document.COLUMN_MIME_TYPE,
                                 DocumentsContract.Document.COLUMN_FLAGS}, null, null, null))
            {
                return oCursor != null && oCursor.moveToFirst() &&
                    DocumentsContract.Document.MIME_TYPE_DIR.equals(oCursor.getString(0)) &&
                    (oCursor.getInt(1) & DocumentsContract.Document.FLAG_DIR_SUPPORTS_CREATE) != 0;
            }
        }
        catch (Exception oError)
        {
            return false;
        }
    }
    public static String persistDirectory(Context oContext, Intent oData)
    {
        try
        {
            Uri oTree = oData.getData();
            if (oTree == null)
            {
                return result("", "", "未能获取目录，请重新选择。").toString();
            }
            rootDocument(oTree.toString());
            int nFlags = oData.getFlags() & (Intent.FLAG_GRANT_READ_URI_PERMISSION |
                                            Intent.FLAG_GRANT_WRITE_URI_PERMISSION);
            oContext.getContentResolver().takePersistableUriPermission(oTree, nFlags);
            if (!directoryAvailable(oContext, oTree.toString()))
            {
                return result("", "", "请选择允许长期读写的目录。").toString();
            }
            return result(oTree.toString(), directoryLabel(oContext, oTree.toString()), "").toString();
        }
        catch (Exception oError)
        {
            return result("", "", "无法取得目录写入权限，请重新选择目录。").toString();
        }
    }
    private static Set<String> childNames(Context oContext, Uri oParent) throws IOException
    {
        Uri oChildren = DocumentsContract.buildChildDocumentsUriUsingTree(oParent,
                DocumentsContract.getDocumentId(oParent));
        Set<String> vNames = new HashSet<String>();
        try (Cursor oCursor = oContext.getContentResolver().query(oChildren,
                new String[]{DocumentsContract.Document.COLUMN_DISPLAY_NAME}, null, null, null))
        {
            if (oCursor == null)
            {
                throw new IOException();
            }
            while (oCursor.moveToNext())
            {
                vNames.add(oCursor.getString(0));
            }
        }
        return vNames;
    }
    private static String documentName(Context oContext, Uri oDocument) throws IOException
    {
        try (Cursor oCursor = oContext.getContentResolver().query(oDocument,
                new String[]{DocumentsContract.Document.COLUMN_DISPLAY_NAME}, null, null, null))
        {
            if (oCursor == null || !oCursor.moveToFirst())
            {
                throw new IOException();
            }
            return oCursor.getString(0);
        }
    }
    private static Uri create(Context oContext, Uri oParent, String strMime, String strName)
            throws IOException
    {
        Uri oCreated = DocumentsContract.createDocument(oContext.getContentResolver(),
                                                        oParent, strMime, strName);
        if (oCreated == null || oCreated.equals(oParent))
        {
            throw new IOException();
        }
        return oCreated;
    }
    private static void copy(Context oContext, File oSource, Uri oTarget, String strRoot,
                             long nToken) throws Exception
    {
        File[] vFiles = oSource.listFiles();
        if (vFiles == null)
        {
            throw new IOException();
        }
        byte[] baBuffer = new byte[65536];
        for (File oFile : vFiles)
        {
            checkContinue(nToken);
            String strCanonical = oFile.getCanonicalPath();
            int nMode = Os.lstat(oFile.getAbsolutePath()).st_mode;
            if (!strCanonical.startsWith(strRoot + File.separator) ||
                    (!OsConstants.S_ISDIR(nMode) && !OsConstants.S_ISREG(nMode)))
            {
                throw new IOException();
            }
            if (OsConstants.S_ISDIR(nMode))
            {
                Uri oDirectory = create(oContext, oTarget,
                        DocumentsContract.Document.MIME_TYPE_DIR, oFile.getName());
                copy(oContext, oFile, oDirectory, strRoot, nToken);
            }
            else
            {
                String strMime = java.net.URLConnection.guessContentTypeFromName(oFile.getName());
                Uri oDocument = create(oContext, oTarget,
                        strMime == null ? "application/octet-stream" : strMime, oFile.getName());
                try (FileInputStream oInput = new FileInputStream(oFile);
                     OutputStream oOutput = oContext.getContentResolver().openOutputStream(oDocument, "w"))
                {
                    if (oOutput == null)
                    {
                        throw new IOException();
                    }
                    int nRead;
                    while ((nRead = oInput.read(baBuffer)) != -1)
                    {
                        checkContinue(nToken);
                        oOutput.write(baBuffer, 0, nRead);
                    }
                    oOutput.flush();
                }
            }
        }
    }
    public static String publish(Context oContext, String strSource, String strTree, long nToken)
    {
        Uri oCreated = null;
        JSONObject oResult;
        boolean bCancelled = false;
        try
        {
            checkContinue(nToken);
            if (!directoryAvailable(oContext, strTree))
            {
                throw new SecurityException();
            }
            File oSource = new File(strSource);
            if (!OsConstants.S_ISDIR(Os.lstat(oSource.getAbsolutePath()).st_mode))
            {
                throw new IOException();
            }
            Uri oRoot = rootDocument(strTree);
            Set<String> vNames = childNames(oContext, oRoot);
            String strName = oSource.getName().replaceAll("[\\\\/:*?\"<>|\\p{Cntrl}]", "_").trim();
            if (strName.length() == 0 || strName.equals(".") || strName.equals(".."))
            {
                strName = "轻小说";
            }
            if (strName.length() > 100)
            {
                strName = strName.substring(0, 100);
            }
            String strCandidate = strName;
            int nIndex = 1;
            while (vNames.contains(strCandidate) && nIndex < 10000)
            {
                strCandidate = strName + " (" + nIndex++ + ")";
            }
            if (vNames.contains(strCandidate))
            {
                throw new IOException();
            }
            checkContinue(nToken);
            oCreated = create(oContext, oRoot, DocumentsContract.Document.MIME_TYPE_DIR, strCandidate);
            copy(oContext, oSource, oCreated, oSource.getCanonicalPath(), nToken);
            checkContinue(nToken);
            return result("", directoryLabel(oContext, strTree) + "/" +
                    documentName(oContext, oCreated), "").toString();
        }
        catch (Cancelled oError)
        {
            bCancelled = true;
            oResult = result("", "", "已取消下载到本地。");
        }
        catch (SecurityException oError)
        {
            oResult = result("", "", "目录授权已失效，请重新选择保存目录。");
        }
        catch (Exception oError)
        {
            oResult = result("", "", "保存失败，请检查目录的写入权限和剩余空间。");
        }
        if (oCreated != null)
        {
            try
            {
                if (!DocumentsContract.deleteDocument(oContext.getContentResolver(), oCreated))
                {
                    throw new IOException();
                }
            }
            catch (Exception oError)
            {
                try
                {
                    oResult.put("error", oResult.optString("error") + "所选目录中可能保留了未完成的小说文件夹。");
                }
                catch (Exception oIgnored)
                {
                }
            }
        }
        try
        {
            oResult.put("cancelled", bCancelled);
        }
        catch (Exception oError)
        {
        }
        return oResult.toString();
    }
}
